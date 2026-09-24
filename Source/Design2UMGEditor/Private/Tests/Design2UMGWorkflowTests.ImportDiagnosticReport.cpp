// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "Misc/AutomationTest.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "Design2UMGSettings.h"
#include "DesignImageBakeUtils.h"
#include "DesignImportPipeline.h"
#include "DesignImportDiagnosticReport.h"
#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Misc/ScopeExit.h"
#include "DesignSchema.h"
#include "DesignSchemaSourceExtensions.h"
#include "FontResolver.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "UMGSchema.h"
#include "UMGPropertyKeys.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"

#if WITH_AUTOMATION_WORKER

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	void AddRuntimeTypeface(UFont* Font, const FString& TypefaceName)
	{
		if (!Font)
		{
			return;
		}

		Font->FontCacheType = EFontCacheType::Runtime;
		Font->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
			FName(*TypefaceName),
			FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
			EFontHinting::Default,
			EFontLoadingPolicy::LazyLoad));
	}

	UFont* CreateUsableTestFont(const FString& PackagePath, const FString& AssetName, const TArray<FString>& Typefaces)
	{
		UPackage* Package = CreatePackage(*PackagePath);
		UFont* Font = NewObject<UFont>(Package, *AssetName, RF_Public | RF_Standalone);
		for (const FString& Typeface : Typefaces)
		{
			AddRuntimeTypeface(Font, Typeface);
		}
		if (Font)
		{
			FAssetRegistryModule::AssetCreated(Font);
		}
		return Font;
	}

	FString FindNewImportReport(
		const FString& ReportDir,
		const TSet<FString>& ExistingReports)
	{
		TArray<FString> ReportsAfter;
		IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
		for (const FString& Candidate : ReportsAfter)
		{
			if (!ExistingReports.Contains(Candidate))
			{
				return Candidate;
			}
		}
		return FString();
	}

	bool LoadImportReportJson(
		FAutomationTestBase& Test,
		const FString& ReportPath,
		TSharedPtr<FJsonObject>& OutRootObject)
	{
		FString ReportJson;
		Test.TestTrue(TEXT("Load import report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
		Test.TestTrue(TEXT("Parse import report json"), FJsonSerializer::Deserialize(Reader, OutRootObject) && OutRootObject.IsValid());
		return OutRootObject.IsValid();
	}

	TArray<TSharedPtr<FJsonValue>> GetFontResolutionResults(const TSharedPtr<FJsonObject>& RootObject)
	{
		const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
		if (!RootObject.IsValid()
			|| !RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject)
			|| !FontResolutionObject)
		{
			return {};
		}
		return (*FontResolutionObject)->GetArrayField(TEXT("results"));
	}

	TSet<FString> GetIssueCodeSet(const TSharedPtr<FJsonObject>& Entry)
	{
		TSet<FString> Codes;
		if (!Entry.IsValid())
		{
			return Codes;
		}
		const TArray<TSharedPtr<FJsonValue>>* IssueValues = nullptr;
		if (Entry->TryGetArrayField(TEXT("issueCodes"), IssueValues) && IssueValues)
		{
			for (const TSharedPtr<FJsonValue>& IssueValue : *IssueValues)
			{
				FString IssueCode;
				if (IssueValue.IsValid() && IssueValue->TryGetString(IssueCode))
				{
					Codes.Add(IssueCode);
				}
			}
		}
		return Codes;
	}

	TSharedPtr<FJsonObject> FindFontResultByNodeId(
		const TArray<TSharedPtr<FJsonValue>>& Results,
		const FString& NodeId,
		const TOptional<int32> RunId = TOptional<int32>())
	{
		for (const TSharedPtr<FJsonValue>& Value : Results)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			FString FoundNodeId;
			if (!Entry.IsValid()
				|| !Entry->TryGetStringField(TEXT("nodeId"), FoundNodeId)
				|| FoundNodeId != NodeId)
			{
				continue;
			}

			double FoundRunId = 0.0;
			const bool bHasRunId = Entry->TryGetNumberField(TEXT("runId"), FoundRunId);
			if (!RunId.IsSet() && !bHasRunId)
			{
				return Entry;
			}
			if (RunId.IsSet() && bHasRunId && static_cast<int32>(FoundRunId) == RunId.GetValue())
			{
				return Entry;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> FindDocumentFontResultByIndex(
		const TArray<TSharedPtr<FJsonValue>>& Results,
		const int32 DocumentFontIndex)
	{
		for (const TSharedPtr<FJsonValue>& Value : Results)
		{
			const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
			FString EntryScope;
			if (!Entry.IsValid()
				|| !Entry->TryGetStringField(TEXT("entryScope"), EntryScope)
				|| EntryScope != TEXT("document-font-clue"))
			{
				continue;
			}

			double FoundIndex = -1.0;
			if (Entry->TryGetNumberField(TEXT("documentFontIndex"), FoundIndex)
				&& static_cast<int32>(FoundIndex) == DocumentFontIndex)
			{
				return Entry;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticImageResultContractTest,
	"Design2UMG.Import.ImportDiagnosticImageResultContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticImageResultContractTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ExistingReportFiles;
	IFileManager::Get().FindFilesRecursive(ExistingReportFiles, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	ExistingReports.Append(ExistingReportFiles);

	FDesignDocument Document;
	Document.MutableSourceDocument().SourceFilePath = TEXT("G:/Fixtures/image_result_contract.json");
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImageResultContract");

	FDesignImageImportResult ImageResult;
	ImageResult.ImportPath = TEXT("/Game/Design2UMG/ImportedImages");
	for (int32 Index = 0; Index < 33; ++Index)
	{
		FDesignImageDiagnostic Success;
		Success.Outcome = EDesignImageDiagnosticOutcome::Imported;
		Success.Resource = Index == 0
			? TEXT("/Game/Textures/T_successfully_failed_name")
			: FString::Printf(TEXT("/Game/Textures/T_Success_%d"), Index);
		Success.Message = FString::Printf(TEXT("Imported texture: %s"), *Success.Resource);
		ImageResult.AddDiagnostic(MoveTemp(Success));
	}
	FDesignImageDiagnostic BudgetFailure;
	BudgetFailure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	BudgetFailure.Category = EDesignImageDiagnosticCategory::Budget;
	BudgetFailure.NodeId = TEXT("late_failure");
	BudgetFailure.NodeName = TEXT("Late Failure");
	BudgetFailure.Resource = TEXT("https://example.com/late.png");
	BudgetFailure.Reason = TEXT("Cumulative image byte budget exceeded");
	BudgetFailure.Message = TEXT("Late image exceeded the operation budget");
	ImageResult.AddDiagnostic(MoveTemp(BudgetFailure));
	ImageResult.RefreshDisplaySummary();

	DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
	Artifacts.ImageResult = ImageResult;
	TestTrue(TEXT("Typed image result report is written"), DesignImportDiagnosticReport::Write(Document, Data, Artifacts));
	const FString ReportPath = FindNewImportReport(ReportDir, ExistingReports);
	TestTrue(TEXT("Typed image result report path is found"), !ReportPath.IsEmpty());

	TSharedPtr<FJsonObject> RootObject;
	if (!LoadImportReportJson(*this, ReportPath, RootObject))
	{
		return false;
	}
	const TSharedPtr<FJsonObject>* ResourcesObject = nullptr;
	if (!TestTrue(TEXT("Report has resources object"), RootObject->TryGetObjectField(TEXT("resources"), ResourcesObject) && ResourcesObject))
	{
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>& Failures = (*ResourcesObject)->GetArrayField(TEXT("imageFailures"));
	TestEqual(TEXT("Only the typed failure is reported"), Failures.Num(), 1);
	if (Failures.Num() == 1)
	{
		const TSharedPtr<FJsonObject> Failure = Failures[0]->AsObject();
		TestEqual(TEXT("A failure after 32 successes keeps its category"), Failure->GetStringField(TEXT("category")), FString(TEXT("budget")));
		TestEqual(TEXT("A failure after 32 successes keeps its node"), Failure->GetObjectField(TEXT("node"))->GetStringField(TEXT("id")), FString(TEXT("late_failure")));
	}
	const FString Summary = (*ResourcesObject)->GetStringField(TEXT("imageImportSummary"));
	TestTrue(TEXT("Summary keeps the total failure count"), Summary.Contains(TEXT("failed=1")));
	TestTrue(TEXT("Summary keeps the failure category count"), Summary.Contains(TEXT("budget=1")));
	TestTrue(TEXT("Summary explicitly reports omitted detail"), Summary.Contains(TEXT("2 image diagnostic detail(s) omitted")));
	IFileManager::Get().Delete(*ReportPath, false, true);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportOutcomeContractTest,
	"Design2UMG.Import.ImportDiagnosticReportOutcomeContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportOutcomeContractTest::RunTest(const FString&)
{
	using Status = EDesign2UMGCommitStatus;
	using File = EDesign2UMGFileState;
	using Memory = EDesign2UMGMemoryState;
	struct FCase
	{
		FDesign2UMGCommitResult Commit;
		const TCHAR* StatusName;
		const TCHAR* Asset;
		const TCHAR* Manifest;
		const TCHAR* MemoryName;
		bool bSucceeded;
	};
	const FCase Cases[] = {
		{{Status::Succeeded, File::Saved, File::Saved, Memory::Modified}, TEXT("succeeded"), TEXT("saved"), TEXT("saved"), TEXT("modified"), true},
		{{Status::SucceededWithDegradation, File::Saved, File::Saved, Memory::Modified}, TEXT("succeeded_with_degradation"), TEXT("saved"), TEXT("saved"), TEXT("modified"), true},
		{{Status::NotCommitted, File::Restored, File::Restored, Memory::Restored}, TEXT("not_committed"), TEXT("restored"), TEXT("restored"), TEXT("restored"), false},
		{{Status::CommittedWithMetadataFailure, File::Saved, File::Indeterminate, Memory::Modified}, TEXT("committed_with_metadata_failure"), TEXT("saved"), TEXT("indeterminate"), TEXT("modified"), false},
		{{Status::RollbackIncomplete, File::Indeterminate, File::Restored, Memory::RecoveryFailed}, TEXT("rollback_incomplete"), TEXT("indeterminate"), TEXT("restored"), TEXT("recovery_failed"), false},
		{{Status::Conflict, File::Unchanged, File::Unchanged, Memory::Unchanged}, TEXT("conflict"), TEXT("unchanged"), TEXT("unchanged"), TEXT("unchanged"), false},
		{{Status::Succeeded, File::Saved, File::Unchanged, Memory::Modified}, TEXT("succeeded"), TEXT("saved"), TEXT("unchanged"), TEXT("modified"), true},
	};
	const FString ReportDir = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ImportReports");
	TArray<FString> Reports;
	IFileManager::Get().FindFilesRecursive(Reports, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports(Reports);
	FDesignDocument Document;
	FUMGWidgetData Data;
	DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
	for (const FCase& Case : Cases)
	{
		Artifacts.Commit = Case.Commit;
		Artifacts.Commit->AssetPath = TEXT("/Game/OutcomeContract.Target");
		TestTrue(TEXT("Report write succeeds"), DesignImportDiagnosticReport::Write(Document, Data, Artifacts));
		const FString Path = FindNewImportReport(ReportDir, ExistingReports);
		ExistingReports.Add(Path);
		TSharedPtr<FJsonObject> Report;
		if (!LoadImportReportJson(*this, Path, Report)) return false;
		const TSharedPtr<FJsonObject>* Outcome = nullptr;
		if (!TestTrue(TEXT("Commit outcome emitted"), Report->TryGetObjectField(TEXT("outcome"), Outcome))) return false;
		TestEqual(TEXT("Commit status serialized"), (*Outcome)->GetStringField(TEXT("status")), FString(Case.StatusName));
		TestEqual(TEXT("Success agrees with commit status"), (*Outcome)->GetBoolField(TEXT("succeeded")), Case.bSucceeded);
		TestEqual(TEXT("Asset state serialized"), (*Outcome)->GetStringField(TEXT("assetState")), FString(Case.Asset));
		TestEqual(TEXT("Manifest state serialized"), (*Outcome)->GetStringField(TEXT("manifestState")), FString(Case.Manifest));
		TestEqual(TEXT("Memory state serialized"), (*Outcome)->GetStringField(TEXT("memoryState")), FString(Case.MemoryName));
		TestEqual(TEXT("Target path does not imply success"), (*Outcome)->GetStringField(TEXT("assetPath")), Artifacts.Commit->AssetPath);
	}
	Artifacts.Commit.Reset();
	DesignImportDiagnosticReport::Write(Document, Data, Artifacts);
	TSharedPtr<FJsonObject> Report;
	if (!LoadImportReportJson(*this, FindNewImportReport(ReportDir, ExistingReports), Report)) return false;
	TestFalse(TEXT("No supplied commit does not invent success"), Report->HasField(TEXT("outcome")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportWriteFailureTest,
	"Design2UMG.Import.ImportDiagnosticReportWriteFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportWriteFailureTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ReportFailure_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse source"), FFigmaParser::ParseDocument(
		MakeFixtureDoc(MakeFixtureText(TEXT("label"), TEXT("Label"), 8, 8, TEXT("Report"))), Document))) return false;
	const FDesign2UMGImportSettingsSnapshot Settings = MakeFixtureSnapshot(nullptr);
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings);
	FArtifactGenerationRequest Request;
	Request.OutputPath = Folder;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	const FArtifactGenerationResult Imported = DesignImportPipeline::GenerateArtifacts(Data, Request, Settings.GenerationSettings);
	if (!TestTrue(TEXT("Real asset and manifest committed"), Imported.Commit.IsSuccess())) return false;
	const FDesign2UMGCommitResult Commit = Imported.Commit;
	const FString PackageName = FPackageName::ObjectPathToPackageName(Commit.AssetPath);
	TMap<FString, TArray<uint8>> OriginalFiles;
	for (const FString& Path : {
		FPackageName::LongPackageNameToFilename(PackageName, FPackageName::GetAssetPackageExtension()),
		Design2UMGGenerationManifest::BuildSidecarPathForAsset(PackageName)})
	{
		if (!TestTrue(TEXT("Read committed file"), FFileHelper::LoadFileToArray(OriginalFiles.Add(Path), *Path))) return false;
	}

	// A real filesystem refusal without changing shared directories or permissions.
	Document.MutableSourceDocument().SourceFilePath = FString::ChrN(512, TCHAR('x')) + TEXT(".pen");
	DesignImportDiagnosticReport::FImportReportArtifacts Artifacts;
	Artifacts.Commit = Commit;
	AddExpectedMessagePlain(TEXT("could not write import diagnostic report"), ELogVerbosity::Warning,
		EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("Import report write failure reaches the caller"),
		DesignImportDiagnosticReport::Write(Document, Data, Artifacts));
	TestFalse(TEXT("Reimport forwards report write failure"),
		DesignImportDiagnosticReport::WriteReimport(Document, Data, Commit.AssetPath, {}, nullptr, &Commit));
	TestEqual(TEXT("Report failure does not change asset commit status"), Artifacts.Commit->Status, Commit.Status);
	TestEqual(TEXT("Saved asset state is preserved"), Artifacts.Commit->Asset, Commit.Asset);
	TestEqual(TEXT("Saved manifest state is preserved"), Artifacts.Commit->Manifest, Commit.Manifest);
	TestEqual(TEXT("Memory state is preserved"), Artifacts.Commit->Memory, Commit.Memory);
	for (const auto& File : OriginalFiles)
	{
		TArray<uint8> After;
		TestTrue(TEXT("Committed file remains readable"), FFileHelper::LoadFileToArray(After, *File.Key));
		TestTrue(TEXT("Report failure preserves committed bytes"), File.Value == After);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportMappingOutcomeTest,
	"Design2UMG.Import.ImportDiagnosticReportMappingOutcome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportMappingOutcomeTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/MappingOutcome");
	FixtureWipeOutput(Folder);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	FDesign2UMGImportSettingsSnapshot Settings = MakeFixtureSnapshot(FixtureMappedClassPath);
	auto& Inputs = Settings.GenerationSettings.ComponentMapping.Table.Entries[0].Inputs;
	auto BoolInput = Inputs[0];
	BoolInput.Target = TEXT("BoolValue");
	BoolInput.Type = DesignComponentMapping::EComponentInputType::Bool;
	Inputs.Insert(BoolInput, 0); // The valid text input must still run after the invalid bool input.
	const auto MakeDocument = [](const TCHAR* Label)
	{
		return MakeFixtureDoc(MakeFixtureButton(TEXT("mapped"), TEXT("Mapped"), 24, 24, Label));
	};
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse source"), FFigmaParser::ParseDocument(MakeDocument(TEXT("not-a-bool")), Document))) return false;
	FUMGWidgetData Data = FDesign2UMGConverter::Convert(Document, Settings.ConversionSettings);
	FArtifactGenerationRequest Request;
	Request.OutputPath = Folder;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.DiagnosticDocument = Document;
	const FString ReportDir = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ImportReports");
	TArray<FString> Reports;
	IFileManager::Get().FindFilesRecursive(Reports, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports(Reports);
	const auto CheckReport = [&](const FDesign2UMGCommitResult& Commit, const TCHAR* Status, const TCHAR* ManifestState)
	{
		const FString Path = FindNewImportReport(ReportDir, ExistingReports);
		ExistingReports.Add(Path);
		TSharedPtr<FJsonObject> Report;
		if (!LoadImportReportJson(*this, Path, Report)) return false;
		const TSharedPtr<FJsonObject>* Outcome = nullptr;
		if (!TestTrue(TEXT("Final pipeline outcome emitted"), Report->TryGetObjectField(TEXT("outcome"), Outcome))) return false;
		TestEqual(TEXT("Pipeline status reaches JSON"), (*Outcome)->GetStringField(TEXT("status")), FString(Status));
		TestEqual(TEXT("Pipeline success reaches JSON"), (*Outcome)->GetBoolField(TEXT("succeeded")), Commit.IsSuccess());
		TestEqual(TEXT("Asset saved despite skipped input"), (*Outcome)->GetStringField(TEXT("assetState")), FString(TEXT("saved")));
		TestEqual(TEXT("Manifest result reaches JSON"), (*Outcome)->GetStringField(TEXT("manifestState")), FString(ManifestState));
		TestEqual(TEXT("Saved memory remains modified"), (*Outcome)->GetStringField(TEXT("memoryState")), FString(TEXT("modified")));
		return true;
	};
	const FArtifactGenerationResult Imported = DesignImportPipeline::GenerateArtifacts(Data, Request, Settings.GenerationSettings);
	TestEqual(TEXT("Skipped import input degrades the commit"), Imported.Commit.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	if (!CheckReport(Imported.Commit, TEXT("succeeded_with_degradation"), TEXT("saved"))) return false;
	UWidgetBlueprint* Blueprint = LoadObject<UWidgetBlueprint>(nullptr, *Imported.GeneratedAssetPath);
	if (!TestNotNull(TEXT("Degraded import remains usable"), Blueprint)) return false;
	UDesign2UMGComponentInputTestWidget* Instance = FixtureFindMappedInstance(Blueprint);
	if (!TestNotNull(TEXT("Mapped instance built"), Instance)) return false;
	TestTrue(TEXT("Invalid bool preserves its default"), Instance->BoolValue);
	TestEqual(TEXT("Later valid input still applied"), Instance->TextValue.ToString(), FString(TEXT("not-a-bool")));
	FDesignReimportRequest ReimportRequest;
	ReimportRequest.TargetAssetPath = Blueprint->GetPathName();
	ReimportRequest.SettingsSnapshot = Settings;
	for (const bool bValid : {true, false})
	{
		FDesignDocument UpdatedDocument;
		if (!TestTrue(TEXT("Parse updated inputs"), FFigmaParser::ParseDocument(
			MakeDocument(bValid ? TEXT("false") : TEXT("invalid-again")), UpdatedDocument))) return false;
		const FDesignReimportResult Result = DesignImportPipeline::Reimport(UpdatedDocument, ReimportRequest);
		TestEqual(TEXT("Reimport exposes only current input degradation"), Result.bHasDegradation, !bValid);
		TestEqual(TEXT("Reimport preserves instance identity"), FixtureFindMappedInstance(Blueprint), Instance);
		TestFalse(TEXT("Invalid input retains the previously applied bool"), Instance->BoolValue);
		if (!CheckReport(Result.Commit, bValid ? TEXT("succeeded") : TEXT("succeeded_with_degradation"), TEXT("saved"))) return false;
	}
	Design2UMGEditorTests::FScopedFileFailures FileFailures;
	FileFailures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
	AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);
	const FDesignReimportResult Failed = DesignImportPipeline::Reimport(Document, ReimportRequest);
	TestEqual(TEXT("Metadata failure outranks input degradation"), Failed.Commit.Status, EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
	TestTrue(TEXT("Metadata failure retains input loss information"), Failed.bHasDegradation);
	return CheckReport(Failed.Commit, TEXT("committed_with_metadata_failure"), TEXT("unchanged"));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportGenerationFailureTest,
	"Design2UMG.Import.ImportDiagnosticReportGenerationFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportGenerationFailureTest::RunTest(const FString&)
{
	for (const EDesignBlueprintGenerationMode Mode : {
		EDesignBlueprintGenerationMode::BlueprintOnly,
		EDesignBlueprintGenerationMode::BlueprintAndPng,
		EDesignBlueprintGenerationMode::BlueprintAndCompiledPng})
	{
		const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG/ImportReports"));
		TArray<FString> ReportsBefore;
		IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
		TSet<FString> ExistingReports;
		for (const FString& Report : ReportsBefore) ExistingReports.Add(Report);

		FUMGWidgetData Data;
		Data.WidgetName = TEXT("WBP_FailedImportReport");
		Data.RootWidget.Name = TEXT("FailedCaptureRoot");
		Data.RootWidget.Type = TEXT("RetainerBox");
		Data.RootWidget.Bounds.W = 0;
		Data.RootWidget.Bounds.H = 100;
		Data.RootWidget.Effect.EffectMaterial = UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
		FArtifactGenerationRequest Request;
		Request.OutputPath = TEXT("/Temp/FailedImportReport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
		Request.Generation.Mode = Mode;
		Request.DiagnosticDocument = FDesignDocument();
		AddExpectedError(TEXT("has no valid capture size"), EAutomationExpectedErrorFlags::Contains, 1);
		if (Mode != EDesignBlueprintGenerationMode::BlueprintAndCompiledPng)
		{
			AddExpectedError(TEXT("DesignImportGeneration: Widget Blueprint build failed"), EAutomationExpectedErrorFlags::Contains, 1);
		}
		const FArtifactGenerationResult Result = DesignImportPipeline::GenerateArtifacts(Data, Request);
		TestFalse(TEXT("Generation fails"), Result.bSucceeded);
		TestTrue(TEXT("Failed import has no generated asset"), Result.GeneratedAssetPath.IsEmpty());
		const FString ReportPath = FindNewImportReport(ReportDir, ExistingReports);
		if (!TestFalse(TEXT("Failed import still writes a diagnostic report"), ReportPath.IsEmpty())) return false;
		TSharedPtr<FJsonObject> Report;
		if (!LoadImportReportJson(*this, ReportPath, Report)) return false;
		const TSharedPtr<FJsonObject>* Outcome = nullptr;
		if (!TestTrue(TEXT("Pipeline report contains final outcome"), Report->TryGetObjectField(TEXT("outcome"), Outcome) && Outcome)) return false;
		TestEqual(TEXT("Failed pipeline report exposes typed commit status"),
			(*Outcome)->GetStringField(TEXT("status")), TEXT("not_committed"));
		TestFalse(TEXT("Failed pipeline report is not reported as success"), (*Outcome)->GetBoolField(TEXT("succeeded")));
		const TSharedPtr<FJsonObject>* Mapping = nullptr;
		if (!TestTrue(TEXT("Report contains component diagnostics"), Report->TryGetObjectField(TEXT("componentMapping"), Mapping))) return false;
		const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
		if (!TestTrue(TEXT("Report contains diagnostic entries"), (*Mapping)->TryGetArrayField(TEXT("entries"), Entries))) return false;
		TestTrue(TEXT("Written report identifies the failed node and concrete reason"), Entries->ContainsByPredicate([](const TSharedPtr<FJsonValue>& Value)
		{
			const TSharedPtr<FJsonObject> Entry = Value->AsObject();
			return Entry->GetStringField(TEXT("kind")) == TEXT("resource_preparation_failed")
				&& Entry->GetStringField(TEXT("message")).Contains(TEXT("FailedCaptureRoot"))
				&& Entry->GetStringField(TEXT("message")).Contains(TEXT("has no valid capture size"));
		}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportStructureTest,
	"Design2UMG.Import.ImportDiagnosticReportStructure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportStructureTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("figma-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FDesignSkippedNodeReport SkippedNode;
	SkippedNode.NodeId = TEXT("slice1");
	SkippedNode.NodeName = TEXT("Slice Overlay");
	SkippedNode.NodeType = TEXT("SLICE");
	SkippedNode.Reason = TEXT("design_only");
	Document.MutableParseDiagnostics().SkippedDesignerNodes.Add(SkippedNode);

	TSharedPtr<FDesignNode> TextPathNode = MakeShared<FDesignNode>();
	TextPathNode->Id = TEXT("text_path_1");
	TextPathNode->Name = TEXT("Curved Label");
	TextPathNode->Type = EDesignNodeType::Text;
	TextPathNode->MutableSource().SourceNodeType = TEXT("TEXT_PATH");
	TextPathNode->Text.FontFamily = TEXT("DefinitelyMissingReportFont");

	TSharedPtr<FDesignNode> RenderFallbackNode = MakeShared<FDesignNode>();
	RenderFallbackNode->Id = TEXT("render_1");
	RenderFallbackNode->Name = TEXT("Blurred Hero");
	RenderFallbackNode->Type = EDesignNodeType::Rectangle;
	RenderFallbackNode->MutableSource().SourceNodeType = TEXT("RECTANGLE");
	RenderFallbackNode->Visual.RenderFallback.ImageRef = TEXT("render_hero");
	RenderFallbackNode->Metadata.Add(TEXT("design2umgRenderFallbackScope"), TEXT("full"));
	RenderFallbackNode->Metadata.Add(TEXT("design2umgRenderFallbackSource"), TEXT("figma_render_png"));
	RenderFallbackNode->Metadata.Add(TEXT("design2umgRenderFallbackReason"), TEXT("foreground_blur"));

	Document.Children.Add(TextPathNode);
	Document.Children.Add(RenderFallbackNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFixture");
	Data.Warnings.Add(TEXT("Figma TEXT_PATH 'Curved Label' -> TextBlock (text-on-path downgraded to regular text)"));

	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("BackgroundBlur");
	Wrapper.Id = TEXT("hero_blur");
	Wrapper.Name = TEXT("HeroBlur");
	Wrapper.Effect.BlurStrength = 12.0f;

	FUMGWidgetNode Child;
	Child.Type = TEXT("TextBlock");
	Child.Id = TEXT("hero_text");
	Child.Name = TEXT("HeroText");
	Wrapper.Children.Add(Child);
	Data.RootWidget = Wrapper;

	FDesignImageImportResult ImageResult;
	ImageResult.ImportPath = TEXT("/Game/Design2UMG/ImportedImages");
	FDesignImageDiagnostic ImageFailure;
	ImageFailure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	ImageFailure.Category = EDesignImageDiagnosticCategory::Download;
	ImageFailure.NodeId = TEXT("hero_image");
	ImageFailure.NodeName = TEXT("Hero");
	ImageFailure.Resource = TEXT("https://example.com/hero.png");
	ImageFailure.Reason = TEXT("Network request failed");
	ImageFailure.Message = TEXT("Image 'Hero' download failed: https://example.com/hero.png (reason: Network request failed)");
	ImageResult.AddDiagnostic(MoveTemp(ImageFailure));
	ImageResult.RefreshDisplaySummary();

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		ImageResult,
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport"),
		TEXT("G:/SampleProject/Saved/Design2UMG/WBP_ImportReportFixture.png"),
		TEXT("/Game/Design2UMG/_TestOutput/ImportReport/WBP_ImportReportFixture"));

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	FDateTime LatestTimestamp = FDateTime::MinValue();
	for (const FString& Candidate : ReportsAfter)
	{
		const FDateTime Timestamp = IFileManager::Get().GetTimeStamp(*Candidate);
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
		if (Timestamp > LatestTimestamp)
		{
			LatestTimestamp = Timestamp;
			ReportPath = Candidate;
		}
	}

	TestTrue(TEXT("Import report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load import report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	if (ReportJson.IsEmpty())
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse import report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		return false;
	}
	TestEqual(TEXT("Report exposes schema version"), RootObject->GetStringField(TEXT("schemaVersion")), TEXT("design2umg-import-report-v2"));
	TestEqual(TEXT("Report exposes its own path"), RootObject->GetStringField(TEXT("reportPath")), ReportPath);

	const TSharedPtr<FJsonObject>* SourceObject = nullptr;
	TestTrue(TEXT("Report has source object"), RootObject->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject);
	if (!SourceObject)
	{
		return false;
	}

	TestEqual(TEXT("Source origin inferred from json input"), (*SourceObject)->GetStringField(TEXT("origin")), TEXT("local_json"));
	TestTrue(TEXT("Source identity emitted"), !(*SourceObject)->GetStringField(TEXT("identity")).IsEmpty());
	TestEqual(TEXT("Top-level sourceFilePath aliases source.filePath"), RootObject->GetStringField(TEXT("sourceFilePath")), (*SourceObject)->GetStringField(TEXT("filePath")));
	TestEqual(TEXT("Top-level sourceIdentity aliases source.identity"), RootObject->GetStringField(TEXT("sourceIdentity")), (*SourceObject)->GetStringField(TEXT("identity")));

	const TSharedPtr<FJsonObject>* DiagnosticsObject = nullptr;
	TestTrue(TEXT("Report has diagnostics object"), RootObject->TryGetObjectField(TEXT("diagnostics"), DiagnosticsObject) && DiagnosticsObject);
	if (!DiagnosticsObject)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Degradations = nullptr;
	TestTrue(TEXT("Diagnostics include degradations"), (*DiagnosticsObject)->TryGetArrayField(TEXT("degradations"), Degradations) && Degradations && Degradations->Num() >= 2);
	TestTrue(TEXT("Diagnostics include warnings"), (*DiagnosticsObject)->GetArrayField(TEXT("warnings")).Num() == 1);
		TestTrue(TEXT("Diagnostics include skipped designer nodes"), (*DiagnosticsObject)->GetArrayField(TEXT("skippedDesignerNodes")).Num() == 1);
	TestFalse(TEXT("Top-level warnings alias is retired"), RootObject->HasField(TEXT("warnings")));
	TestFalse(TEXT("Top-level skipped nodes alias is retired"), RootObject->HasField(TEXT("skippedDesignerNodes")));

	bool bFoundTextPathFallback = false;
	bool bFoundRenderFallback = false;
	for (const TSharedPtr<FJsonValue>& Value : *Degradations)
	{
		const TSharedPtr<FJsonObject> Entry = Value.IsValid() ? Value->AsObject() : nullptr;
		if (!Entry.IsValid())
		{
			continue;
		}

		const FString Kind = Entry->GetStringField(TEXT("kind"));
		bFoundTextPathFallback |= Kind == TEXT("text_path_fallback");
		bFoundRenderFallback |= Kind == TEXT("render_fallback");
	}
	TestTrue(TEXT("Report records text path fallback"), bFoundTextPathFallback);
	TestTrue(TEXT("Report records render fallback"), bFoundRenderFallback);

	const TSharedPtr<FJsonObject>* EffectsObject = nullptr;
	TestTrue(TEXT("Report has effects object"), RootObject->TryGetObjectField(TEXT("effects"), EffectsObject) && EffectsObject);
	TestEqual(TEXT("Report records effect wrapper count"), static_cast<int32>((*EffectsObject)->GetNumberField(TEXT("wrapperCount"))), 1);
	TestEqual(TEXT("Report records wrapper entry"), (*EffectsObject)->GetArrayField(TEXT("wrappers")).Num(), 1);

	const TSharedPtr<FJsonObject>* ResourcesObject = nullptr;
	TestTrue(TEXT("Report has resources object"), RootObject->TryGetObjectField(TEXT("resources"), ResourcesObject) && ResourcesObject);
	TestEqual(TEXT("Report records missing fonts"), (*ResourcesObject)->GetArrayField(TEXT("missingFonts")).Num(), 1);
	TestEqual(TEXT("Report records image failures"), (*ResourcesObject)->GetArrayField(TEXT("imageFailures")).Num(), 1);
	const TSharedPtr<FJsonObject> ImageFailureObject = (*ResourcesObject)->GetArrayField(TEXT("imageFailures"))[0]->AsObject();
	TestEqual(TEXT("Image failure has typed outcome"), ImageFailureObject->GetStringField(TEXT("outcome")), FString(TEXT("failed")));
	TestEqual(TEXT("Image failure has typed category"), ImageFailureObject->GetStringField(TEXT("category")), FString(TEXT("download")));
	TestEqual(TEXT("Image failure has structured resource"), ImageFailureObject->GetStringField(TEXT("resource")), FString(TEXT("https://example.com/hero.png")));
	TestEqual(TEXT("Image failure has structured reason"), ImageFailureObject->GetStringField(TEXT("reason")), FString(TEXT("Network request failed")));
	const TSharedPtr<FJsonObject>* ImageNodeObject = nullptr;
	TestTrue(TEXT("Image failure has structured node"), ImageFailureObject->TryGetObjectField(TEXT("node"), ImageNodeObject) && ImageNodeObject);
	if (ImageNodeObject)
	{
		TestEqual(TEXT("Image failure node id is preserved"), (*ImageNodeObject)->GetStringField(TEXT("id")), FString(TEXT("hero_image")));
	}
	const TSharedPtr<FJsonObject>* ImageImportObject = nullptr;
	TestTrue(TEXT("Report has structured image import result"), (*ResourcesObject)->TryGetObjectField(TEXT("imageImport"), ImageImportObject) && ImageImportObject);
	if (ImageImportObject)
	{
		TestEqual(TEXT("Structured image failed count is preserved"), static_cast<int32>((*ImageImportObject)->GetNumberField(TEXT("failedCount"))), 1);
	}
	TestFalse(TEXT("Top-level missingFonts alias is retired"), RootObject->HasField(TEXT("missingFonts")));
	TestFalse(TEXT("Top-level imageImportReport alias is retired"), RootObject->HasField(TEXT("imageImportReport")));
	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Missing project font is represented in font results"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Missing font falls back with warning state"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("default-font"));
			TestEqual(TEXT("Missing font reports source declaration"),
				FontResult->GetStringField(TEXT("declarationSourceKind")),
				TEXT("source-declaration"));
			TestTrue(TEXT("Missing font reports a fallback source"),
				FontResult->GetStringField(TEXT("resolutionSourceKind")) == TEXT("default-font-asset")
				|| FontResult->GetStringField(TEXT("resolutionSourceKind")) == TEXT("engine-default"));
			TestTrue(TEXT("Missing font does not report fallback as resolved asset"),
				!FontResult->HasField(TEXT("resolvedAssetPath")));
			TestEqual(TEXT("Font result key is stable"),
				FontResult->GetStringField(TEXT("declarationKey")),
				TEXT("definitelymissingreportfont|none|400|normal"));
		}
	}

	const TSharedPtr<FJsonObject>* OutputObject = nullptr;
	TestTrue(TEXT("Report has output object"), RootObject->TryGetObjectField(TEXT("output"), OutputObject) && OutputObject);
	if (OutputObject)
	{
		TestEqual(TEXT("Top-level requestedOutputPath aliases output.requestedOutputPath"), RootObject->GetStringField(TEXT("requestedOutputPath")), (*OutputObject)->GetStringField(TEXT("requestedOutputPath")));
		TestEqual(TEXT("Top-level requestedRenderPngPath aliases output.requestedRenderPngPath"), RootObject->GetStringField(TEXT("requestedRenderPngPath")), (*OutputObject)->GetStringField(TEXT("requestedRenderPngPath")));
	}

	const TArray<TSharedPtr<FJsonValue>>* Recommendations = nullptr;
	TestTrue(TEXT("Report includes recommendations"), RootObject->TryGetArrayField(TEXT("recommendations"), Recommendations) && Recommendations && Recommendations->Num() > 0);
	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportEngineFontFallbackWarnsTest,
	"Design2UMG.Import.ImportDiagnosticReportEngineFontFallbackWarns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportEngineFontFallbackWarnsTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("engine-font-fallback-report-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("engine_fallback_text");
	TextNode->Name = TEXT("Engine Fallback Text");
	TextNode->Type = EDesignNodeType::Text;
	// Use an absent family: imported project fonts must not accidentally satisfy the fallback fixture.
	TextNode->Text.FontFamily = TEXT("D2UAbsentFont_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TestTrue(TEXT("Fixture family has no matching asset"), FFontResolver::FindFontAssetPath(TextNode->Text.FontFamily).IsEmpty());
	TextNode->Text.FontWeight = TEXT("500");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_EngineFontFallbackReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/EngineFontFallback"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/EngineFontFallback/WBP_EngineFontFallbackReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Engine fallback report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load engine fallback report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse engine fallback report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Engine fallback emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Engine fallback remains a warning fallback"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("default-font"));
			TestEqual(TEXT("Engine fallback keeps warning severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("warning"));
			TestFalse(TEXT("Engine fallback must not report a resolved asset"),
				FontResult->HasField(TEXT("resolvedAssetPath")));
			TestEqual(TEXT("Engine fallback reports the fallback engine font asset"),
				FontResult->GetStringField(TEXT("fallbackAssetPath")),
				TEXT("/Engine/EngineFonts/Roboto.Roboto"));
			TestEqual(TEXT("Engine fallback uses structured source kind"),
				FontResult->GetStringField(TEXT("resolutionSourceKind")),
				TEXT("engine-default"));
			TestEqual(TEXT("Engine fallback uses structured fallback state"),
				FontResult->GetStringField(TEXT("fallbackState")),
				TEXT("engine-default"));
			TestTrue(TEXT("Engine fallback records engine-default-used"),
				GetIssueCodeSet(FontResult).Contains(TEXT("engine-default-used")));
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportInvalidExplicitFontMappingTest,
	"Design2UMG.Import.ImportDiagnosticReportInvalidExplicitFontMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportInvalidExplicitFontMappingTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* EmptyFontPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_EmptyCompositeReportFont"));
	UFont* EmptyCompositeFont = NewObject<UFont>(
		EmptyFontPackage,
		TEXT("D2U_EmptyCompositeReportFont"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Invalid explicit mapping fixture font created"), EmptyCompositeFont != nullptr);
	if (!EmptyCompositeFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	Settings->ExplicitFontMappings.Add(
		TEXT("Inter"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(EmptyCompositeFont)));
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("invalid-explicit-font-mapping-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("explicit_mapping_text");
	TextNode->Name = TEXT("Explicit Mapping Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("Inter");
	TextNode->Text.FontWeight = TEXT("700");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_InvalidExplicitFontMappingReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/InvalidExplicitFontMapping"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/InvalidExplicitFontMapping/WBP_InvalidExplicitFontMappingReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Invalid explicit mapping report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load invalid explicit mapping report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse invalid explicit mapping report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Invalid explicit mapping emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Invalid explicit mapping is reported as rejected"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("rejected"));
			TestEqual(TEXT("Invalid explicit mapping reports error severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("error"));
			TestFalse(TEXT("Invalid explicit mapping must not report an auto-substituted font asset"),
				FontResult->HasField(TEXT("resolvedAssetPath")));

			TestEqual(TEXT("Invalid explicit mapping reports unresolved source kind"),
				FontResult->GetStringField(TEXT("resolutionSourceKind")),
				TEXT("unresolved"));
			TestEqual(TEXT("Invalid explicit mapping records unresolved token"),
				FontResult->GetStringField(TEXT("unresolvedToken")),
				TEXT("unresolved-declaration"));
			TestTrue(TEXT("Invalid explicit mapping issue code is present"),
				GetIssueCodeSet(FontResult).Contains(TEXT("unresolved-declaration")));
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportExplicitMappingMissingTypefaceTest,
	"Design2UMG.Import.ImportDiagnosticReportExplicitMappingMissingTypeface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportExplicitMappingMissingTypefaceTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UPackage* RegularOnlyPackage = CreatePackage(TEXT("/Game/Design2UMG/Fonts/D2U_RegularOnlyExplicitReportFont"));
	UFont* RegularOnlyFont = NewObject<UFont>(
		RegularOnlyPackage,
		TEXT("D2U_RegularOnlyExplicitReportFont"),
		RF_Public | RF_Standalone);
	TestTrue(TEXT("Regular-only explicit report font created"), RegularOnlyFont != nullptr);
	if (!RegularOnlyFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		return false;
	}

	RegularOnlyFont->FontCacheType = EFontCacheType::Runtime;
	RegularOnlyFont->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.Add(FTypefaceEntry(
		FName(TEXT("Regular")),
		FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")),
		EFontHinting::Default,
		EFontLoadingPolicy::LazyLoad));

	Settings->ExplicitFontMappings.Add(
		TEXT("D2URegularOnlyPS"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(RegularOnlyFont)));
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("explicit-missing-typeface-report-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("explicit_mapping_missing_typeface_text");
	TextNode->Name = TEXT("Explicit Mapping Missing Typeface Text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("D2URegularOnly");
	TextNode->Text.FontPostScriptName = TEXT("D2URegularOnlyPS");
	TextNode->Text.FontWeight = TEXT("700");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ExplicitMappingMissingTypefaceReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TextNode->Name;

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/ExplicitMappingMissingTypeface"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/ExplicitMappingMissingTypeface/WBP_ExplicitMappingMissingTypefaceReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);

	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Explicit missing typeface report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load explicit missing typeface report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse explicit missing typeface report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* FontResolutionObject = nullptr;
	TestTrue(TEXT("Report has compact font resolution object"),
		RootObject->TryGetObjectField(TEXT("fontResolution"), FontResolutionObject) && FontResolutionObject);
	if (FontResolutionObject)
	{
		const TArray<TSharedPtr<FJsonValue>>& FontResults = (*FontResolutionObject)->GetArrayField(TEXT("results"));
		TestEqual(TEXT("Missing explicit typeface emits one font result"), FontResults.Num(), 1);
		if (FontResults.Num() == 1 && FontResults[0].IsValid())
		{
			const TSharedPtr<FJsonObject> FontResult = FontResults[0]->AsObject();
			TestEqual(TEXT("Missing explicit typeface is reported as rejected"),
				FontResult->GetStringField(TEXT("resolutionState")),
				TEXT("rejected"));
			TestEqual(TEXT("Missing explicit typeface reports error severity"),
				FontResult->GetStringField(TEXT("severity")),
				TEXT("error"));
			TestFalse(TEXT("Missing explicit typeface must not report an auto-substituted font asset"),
				FontResult->HasField(TEXT("resolvedAssetPath")));
			TestFalse(TEXT("Missing explicit typeface must not report an auto-substituted typeface"),
				FontResult->HasField(TEXT("resolvedTypeface")));

			TestEqual(TEXT("Missing explicit typeface reports unresolved source kind"),
				FontResult->GetStringField(TEXT("resolutionSourceKind")),
				TEXT("unresolved"));
			TestEqual(TEXT("Missing explicit typeface records rejected typeface issue"),
				FontResult->GetStringField(TEXT("unresolvedToken")),
				TEXT("unresolved-declaration"));
			TestTrue(TEXT("Missing explicit typeface issue code is present"),
				GetIssueCodeSet(FontResult).Contains(TEXT("rejected-typeface")));
		}
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportFontIdentityEvidenceTest,
	"Design2UMG.Import.ImportDiagnosticReportFontIdentityEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportFontIdentityEvidenceTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();

	UFont* ExactFont = CreateUsableTestFont(
		TEXT("/Game/Design2UMG/Fonts/D2U_FontIdentityExactReportFont"),
		TEXT("D2U_FontIdentityExactReportFont"),
		{TEXT("Regular"), TEXT("Bold")});
	UFont* DefaultFont = CreateUsableTestFont(
		TEXT("/Game/Design2UMG/Fonts/D2U_FontIdentityDefaultReportFont"),
		TEXT("D2U_FontIdentityDefaultReportFont"),
		{TEXT("Regular")});
	UFont* DefaultFaceFont = CreateUsableTestFont(
		TEXT("/Game/Design2UMG/Fonts/D2U_EmptyWeightDefaultFace"),
		TEXT("D2U_EmptyWeightDefaultFace"),
		{TEXT("Book")});
	TestTrue(TEXT("Exact fixture font created"), ExactFont != nullptr);
	TestTrue(TEXT("Default fixture font created"), DefaultFont != nullptr);
	TestTrue(TEXT("Empty-weight default-face fixture font created"), DefaultFaceFont != nullptr);
	if (!ExactFont || !DefaultFont || !DefaultFaceFont)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		FFontResolver::ClearCache();
		return false;
	}

	Settings->ExplicitFontMappings.Add(
		TEXT("D2UFontIdentityPS"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(ExactFont)));
	Settings->ExplicitFontMappings.Add(
		TEXT("D2UFontIdentity"),
		TSoftObjectPtr<UFont>(FSoftObjectPath(ExactFont)));
	Settings->DefaultFontAsset = TSoftObjectPtr<UFont>(FSoftObjectPath(DefaultFont));
	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("font-identity-evidence-v1");

	FDesignFont UrlClue;
	UrlClue.Name = TEXT("D2UFontIdentity");
	UrlClue.Url = TEXT("https://example.com/fonts/D2UFontIdentity.woff2");
	Document.Fonts.Add(UrlClue);

	FDesignFont UnsupportedFileClue;
	UnsupportedFileClue.Name = TEXT("D2UUnsupportedFileOnly");
	UnsupportedFileClue.Url = TEXT("C:/fonts/D2UUnsupportedFileOnly.ttf");
	Document.Fonts.Add(UnsupportedFileClue);

	FDesignFont EmptyWeightClue;
	EmptyWeightClue.Name = TEXT("D2U_EmptyWeightDefaultFace");
	Document.Fonts.Add(EmptyWeightClue);

	TSharedPtr<FDesignNode> NodeOnly = MakeShared<FDesignNode>();
	NodeOnly->Id = TEXT("font_identity_node_only");
	NodeOnly->Name = TEXT("Font Identity Node Only");
	NodeOnly->Type = EDesignNodeType::Text;
	NodeOnly->Text.FontFamily = TEXT("D2UFontIdentity");
	NodeOnly->Text.FontPostScriptName = TEXT("D2UFontIdentityPS");
	NodeOnly->Text.FontWeight = TEXT("700");
	Document.Children.Add(NodeOnly);

	TSharedPtr<FDesignNode> DuplicateA = MakeShared<FDesignNode>();
	DuplicateA->Id = TEXT("duplicate_a");
	DuplicateA->Type = EDesignNodeType::Text;
	DuplicateA->Text.FontFamily = TEXT("D2UFontIdentity");
	DuplicateA->Text.FontPostScriptName = TEXT("D2UFontIdentityPS");
	DuplicateA->Text.FontWeight = TEXT("700");
	Document.Children.Add(DuplicateA);

	TSharedPtr<FDesignNode> DuplicateB = MakeShared<FDesignNode>();
	DuplicateB->Id = TEXT("duplicate_b");
	DuplicateB->Type = EDesignNodeType::Text;
	DuplicateB->Text.FontFamily = TEXT("D2UFontIdentity");
	DuplicateB->Text.FontPostScriptName = TEXT("D2UFontIdentityPS");
	DuplicateB->Text.FontWeight = TEXT("700");
	Document.Children.Add(DuplicateB);

	TSharedPtr<FDesignNode> RunNode = MakeShared<FDesignNode>();
	RunNode->Id = TEXT("font_identity_runs");
	RunNode->Name = TEXT("Font Identity Runs");
	RunNode->Type = EDesignNodeType::Text;
	FDesignTextRun& Run0 = RunNode->Text.TextRuns.AddDefaulted_GetRef();
	Run0.Content = TEXT("A");
	Run0.FontFamily = TEXT("D2UFontIdentity");
	Run0.FontPostScriptName = TEXT("D2UFontIdentityPS");
	Run0.FontWeight = TEXT("400");
	FDesignTextRun& Run1 = RunNode->Text.TextRuns.AddDefaulted_GetRef();
	Run1.Content = TEXT("B");
	Run1.FontFamily = TEXT("D2UFontIdentity");
	Run1.FontPostScriptName = TEXT("D2UFontIdentityPS");
	Run1.FontWeight = TEXT("700");
	Document.Children.Add(RunNode);

	TSharedPtr<FDesignNode> DefaultFallbackNode = MakeShared<FDesignNode>();
	DefaultFallbackNode->Id = TEXT("default_fallback_text");
	DefaultFallbackNode->Type = EDesignNodeType::Text;
	DefaultFallbackNode->Text.FontFamily = TEXT("D2UMissingDefaultFallback");
	Document.Children.Add(DefaultFallbackNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_FontIdentityEvidenceReportFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/FontIdentityEvidence"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/FontIdentityEvidence/WBP_FontIdentityEvidenceReportFixture"));

	const FString ReportPath = FindNewImportReport(ReportDir, ExistingReports);
	TestTrue(TEXT("Font identity report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		FFontResolver::ClearCache();
		return false;
	}

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	TSharedPtr<FJsonObject> RootObject;
	if (!LoadImportReportJson(*this, ReportPath, RootObject))
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> FontResults = GetFontResolutionResults(RootObject);
	TestEqual(TEXT("Duplicate declarations stay separate across document clue, nodes, and runs"), FontResults.Num(), 9);

	const TSharedPtr<FJsonObject> NodeOnlyResult = FindFontResultByNodeId(FontResults, TEXT("font_identity_node_only"));
	TestTrue(TEXT("Node-only result is present"), NodeOnlyResult.IsValid());
	if (NodeOnlyResult.IsValid())
	{
		TestEqual(TEXT("Node-only entry scope"), NodeOnlyResult->GetStringField(TEXT("entryScope")), TEXT("text-node"));
		TestFalse(TEXT("Node-only entry omits runId"), NodeOnlyResult->HasField(TEXT("runId")));
		TestEqual(TEXT("Explicit PostScript mapping source kind"),
			NodeOnlyResult->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("explicit-project-mapping"));
		TestEqual(TEXT("Explicit PostScript mapping reason"),
			NodeOnlyResult->GetStringField(TEXT("matchReason")),
			TEXT("explicit-postscript-mapping"));
		TestEqual(TEXT("Exact mapping fallback state"),
			NodeOnlyResult->GetStringField(TEXT("fallbackState")),
			TEXT("exact"));
		TestTrue(TEXT("Exact mapping writes resolved asset"),
			NodeOnlyResult->HasField(TEXT("resolvedAssetPath")));
	}

	TestTrue(TEXT("Duplicate node A has its own evidence"),
		FindFontResultByNodeId(FontResults, TEXT("duplicate_a")).IsValid());
	TestTrue(TEXT("Duplicate node B has its own evidence"),
		FindFontResultByNodeId(FontResults, TEXT("duplicate_b")).IsValid());

	const TSharedPtr<FJsonObject> Run0Result = FindFontResultByNodeId(FontResults, TEXT("font_identity_runs"), 0);
	const TSharedPtr<FJsonObject> Run1Result = FindFontResultByNodeId(FontResults, TEXT("font_identity_runs"), 1);
	TestTrue(TEXT("Run 0 result is present"), Run0Result.IsValid());
	TestTrue(TEXT("Run 1 result is present"), Run1Result.IsValid());
	if (Run0Result.IsValid() && Run1Result.IsValid())
	{
		TestEqual(TEXT("Run 0 uses stable runId"), static_cast<int32>(Run0Result->GetNumberField(TEXT("runId"))), 0);
		TestEqual(TEXT("Run 1 uses stable runId"), static_cast<int32>(Run1Result->GetNumberField(TEXT("runId"))), 1);
		TestEqual(TEXT("Run 0 keeps declared weight"), Run0Result->GetStringField(TEXT("declaredWeight")), TEXT("400"));
		TestEqual(TEXT("Run 1 keeps declared weight"), Run1Result->GetStringField(TEXT("declaredWeight")), TEXT("700"));
	}

	const TSharedPtr<FJsonObject> DocumentUrlClue = FindDocumentFontResultByIndex(FontResults, 0);
	TestTrue(TEXT("Document URL clue is present"), DocumentUrlClue.IsValid());
	if (DocumentUrlClue.IsValid())
	{
		TestEqual(TEXT("Document clue entry scope"),
			DocumentUrlClue->GetStringField(TEXT("entryScope")),
			TEXT("document-font-clue"));
		TestFalse(TEXT("Document clue omits nodeId"), DocumentUrlClue->HasField(TEXT("nodeId")));
		TestFalse(TEXT("Document clue omits runId"), DocumentUrlClue->HasField(TEXT("runId")));
		TestTrue(TEXT("URL clue records ignored issue"),
			GetIssueCodeSet(DocumentUrlClue).Contains(TEXT("font-url-ignored")));
		TestEqual(TEXT("URL clue keeps resolver choice"),
			DocumentUrlClue->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("explicit-project-mapping"));
	}

	const TSharedPtr<FJsonObject> UnsupportedFileClueResult = FindDocumentFontResultByIndex(FontResults, 1);
	TestTrue(TEXT("Unsupported file clue is present"), UnsupportedFileClueResult.IsValid());
	if (UnsupportedFileClueResult.IsValid())
	{
		TestEqual(TEXT("Unsupported file clue is unresolved"),
			UnsupportedFileClueResult->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("unresolved"));
		TestEqual(TEXT("Unsupported file clue fallback state"),
			UnsupportedFileClueResult->GetStringField(TEXT("fallbackState")),
			TEXT("unsupported-input"));
		TestEqual(TEXT("Unsupported file clue writes unresolved token"),
			UnsupportedFileClueResult->GetStringField(TEXT("unresolvedToken")),
			TEXT("unsupported-input"));
		TestTrue(TEXT("Unsupported file clue records issue"),
			GetIssueCodeSet(UnsupportedFileClueResult).Contains(TEXT("unsupported-font-file-input")));
	}

	const TSharedPtr<FJsonObject> EmptyWeightClueResult = FindDocumentFontResultByIndex(FontResults, 2);
	TestTrue(TEXT("Empty-weight document font clue is present"), EmptyWeightClueResult.IsValid());
	if (EmptyWeightClueResult.IsValid())
	{
		TestTrue(TEXT("Empty-weight document font clue keeps source weight empty"),
			EmptyWeightClueResult->GetStringField(TEXT("declaredWeight")).IsEmpty());
		TestEqual(TEXT("Empty-weight document font clue does not normalize missing weight to 400"),
			EmptyWeightClueResult->GetStringField(TEXT("declarationKey")),
			TEXT("d2uemptyweightdefaultface|none|none|normal"));
		TestEqual(TEXT("Empty-weight document font clue keeps mapped asset"),
			EmptyWeightClueResult->GetStringField(TEXT("resolvedAssetPath")),
			DefaultFaceFont->GetPathName());
		TestEqual(TEXT("Empty-weight document font clue resolves through the project asset"),
			EmptyWeightClueResult->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("project-asset"));
		TestFalse(TEXT("Empty-weight document font clue leaves mapped default face implicit"),
			EmptyWeightClueResult->HasField(TEXT("resolvedTypeface")));
		TestFalse(TEXT("Empty-weight document font clue does not reject the mapped default face"),
			GetIssueCodeSet(EmptyWeightClueResult).Contains(TEXT("rejected-typeface")));
		TestFalse(TEXT("Empty-weight document font clue does not substitute an undeclared typeface"),
			GetIssueCodeSet(EmptyWeightClueResult).Contains(TEXT("typeface-substituted")));
	}

	const TSharedPtr<FJsonObject> DefaultFallbackResult = FindFontResultByNodeId(FontResults, TEXT("default_fallback_text"));
	TestTrue(TEXT("Default fallback result is present"), DefaultFallbackResult.IsValid());
	if (DefaultFallbackResult.IsValid())
	{
		TestEqual(TEXT("Default fallback source kind"),
			DefaultFallbackResult->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("default-font-asset"));
		TestEqual(TEXT("Default fallback state"),
			DefaultFallbackResult->GetStringField(TEXT("fallbackState")),
			TEXT("default-font-asset"));
		TestTrue(TEXT("Default fallback writes fallback asset path"),
			DefaultFallbackResult->HasField(TEXT("fallbackAssetPath")));
		TestFalse(TEXT("Default fallback does not write resolved asset path"),
			DefaultFallbackResult->HasField(TEXT("resolvedAssetPath")));
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportAmbiguousFontIdentityEvidenceTest,
	"Design2UMG.Import.ImportDiagnosticReportAmbiguousFontIdentityEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportAmbiguousFontIdentityEvidenceTest::RunTest(const FString& Parameters)
{
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	TestTrue(TEXT("Settings available"), Settings != nullptr);
	if (!Settings)
	{
		return false;
	}

	const TMap<FString, TSoftObjectPtr<UFont>> SavedMappings = Settings->ExplicitFontMappings;
	const TSoftObjectPtr<UFont> SavedDefaultFont = Settings->DefaultFontAsset;
	Settings->ExplicitFontMappings.Empty();
	Settings->DefaultFontAsset.Reset();

	UFont* AmbiguousA = CreateUsableTestFont(
		TEXT("/Game/Design2UMG/Fonts/FontIdentityAmbiguousA/D2U_FontIdentityAmbiguous"),
		TEXT("D2U_FontIdentityAmbiguous"),
		{TEXT("Regular")});
	UFont* AmbiguousB = CreateUsableTestFont(
		TEXT("/Game/Design2UMG/Fonts/FontIdentityAmbiguousB/D2U_FontIdentityAmbiguous"),
		TEXT("D2U_FontIdentityAmbiguous"),
		{TEXT("Regular")});
	TestTrue(TEXT("Ambiguous fixture font A created"), AmbiguousA != nullptr);
	TestTrue(TEXT("Ambiguous fixture font B created"), AmbiguousB != nullptr);
	if (!AmbiguousA || !AmbiguousB)
	{
		Settings->ExplicitFontMappings = SavedMappings;
		Settings->DefaultFontAsset = SavedDefaultFont;
		FFontResolver::ClearCache();
		return false;
	}

	FFontResolver::ClearCache();

	FDesignDocument Document;
	Document.Version = TEXT("ambiguous-font-identity-evidence-v1");
	TSharedPtr<FDesignNode> TextNode = MakeShared<FDesignNode>();
	TextNode->Id = TEXT("ambiguous_font_text");
	TextNode->Type = EDesignNodeType::Text;
	TextNode->Text.FontFamily = TEXT("D2U_FontIdentityAmbiguous");
	Document.Children.Add(TextNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_AmbiguousFontIdentityEvidenceReportFixture");
	Data.RootWidget.Type = TEXT("TextBlock");
	Data.RootWidget.Id = TextNode->Id;
	Data.RootWidget.Name = TEXT("Ambiguous Font");

	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/AmbiguousFontIdentityEvidence"),
		FString(),
		TEXT("/Game/Design2UMG/_TestOutput/AmbiguousFontIdentityEvidence/WBP_AmbiguousFontIdentityEvidenceReportFixture"));

	Settings->ExplicitFontMappings = SavedMappings;
	Settings->DefaultFontAsset = SavedDefaultFont;
	FFontResolver::ClearCache();

	const FString ReportPath = FindNewImportReport(ReportDir, ExistingReports);
	TestTrue(TEXT("Ambiguous font report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	TSharedPtr<FJsonObject> RootObject;
	if (!LoadImportReportJson(*this, ReportPath, RootObject))
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>> FontResults = GetFontResolutionResults(RootObject);
	TestEqual(TEXT("Ambiguous font emits one result"), FontResults.Num(), 1);
	const TSharedPtr<FJsonObject> AmbiguousResult = FindFontResultByNodeId(FontResults, TEXT("ambiguous_font_text"));
	TestTrue(TEXT("Ambiguous result is present"), AmbiguousResult.IsValid());
	if (AmbiguousResult.IsValid())
	{
		TestTrue(TEXT("Ambiguous same-priority match records issue"),
			GetIssueCodeSet(AmbiguousResult).Contains(TEXT("ambiguous-font-match")));
		TestEqual(TEXT("Ambiguous source kind remains project asset"),
			AmbiguousResult->GetStringField(TEXT("resolutionSourceKind")),
			TEXT("project-asset"));
		TestFalse(TEXT("Ambiguous match is not exact"),
			AmbiguousResult->GetStringField(TEXT("fallbackState")) == TEXT("exact"));
	}

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportTypedSourcePathContractTest,
	"Design2UMG.Import.ImportDiagnosticReportTypedSourcePathContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportTypedSourcePathContractTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("typed-source-path-v1");
	Document.MutableSourceDocument().SourceFilePath =
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("typed_source_only.pen"));

	const FString ExpectedIdentity = DesignImageBakeUtils::BuildSourceDocumentIdentity(Document.SourceDocument().SourceFilePath);
	TestEqual(
		TEXT("Typed source path drives image bake identity"),
		DesignImageBakeUtils::ResolveDocumentImageBakeIdentity(Document, FString()),
		ExpectedIdentity);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_TypedSourcePathFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 64.0f, 64.0f};

	DesignImportPipeline::WriteImportDiagnosticReport(
		Document,
		Data,
		FDesignImageImportResult(),
		TEXT("/Game/Design2UMG/_TestOutput/TypedSourcePath"),
		TEXT("G:/SampleProject/Saved/Design2UMG/WBP_TypedSourcePathFixture.png"),
		TEXT("/Game/Design2UMG/_TestOutput/TypedSourcePath/WBP_TypedSourcePathFixture"));

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Typed source path report was generated"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		return false;
	}

	FString ReportJson;
	TestTrue(TEXT("Load typed-source report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse typed-source report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (!RootObject.IsValid())
	{
		IFileManager::Get().Delete(*ReportPath);
		return false;
	}

	const TSharedPtr<FJsonObject>* SourceObject = nullptr;
	TestTrue(TEXT("Typed source path report has source object"), RootObject->TryGetObjectField(TEXT("source"), SourceObject) && SourceObject);
	if (SourceObject)
	{
		TestEqual(TEXT("Typed source path surfaces source.filePath"), (*SourceObject)->GetStringField(TEXT("filePath")), Document.SourceDocument().SourceFilePath);
		TestEqual(TEXT("Typed source path surfaces source.identity"), (*SourceObject)->GetStringField(TEXT("identity")), ExpectedIdentity);
	}
	TestEqual(TEXT("Typed source path keeps top-level sourceFilePath alias"), RootObject->GetStringField(TEXT("sourceFilePath")), Document.SourceDocument().SourceFilePath);
	TestEqual(TEXT("Typed source path keeps top-level sourceIdentity alias"), RootObject->GetStringField(TEXT("sourceIdentity")), ExpectedIdentity);

	IFileManager::Get().Delete(*ReportPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportExecutePathTest,
	"Design2UMG.Import.ImportDiagnosticReportExecutePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportExecutePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString& ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	TSharedPtr<FDesignNode> RootNode = MakeShared<FDesignNode>();
	RootNode->Id = TEXT("root");
	RootNode->Name = TEXT("Root");
	RootNode->Type = EDesignNodeType::Frame;
	RootNode->Transform.Width = FDesignSize::Fixed(120.0f);
	RootNode->Transform.Height = FDesignSize::Fixed(60.0f);
	Document.Children.Add(RootNode);

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportExecuteFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportExecute");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("Execute data path succeeds"), Result.bSucceeded);
	if (!Result.bSucceeded)
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Execute data path writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (ReportPath.IsEmpty() || !FPaths::FileExists(ReportPath))
	{
		DeleteGeneratedAssetsAtPath(Request.OutputPath);
		return false;
	}

	const TArray<uint8> ReportBytes = [&ReportPath]()
	{
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *ReportPath);
		return Bytes;
	}();
	TestFalse(TEXT("Execute data path report is UTF-8 without BOM"),
		ReportBytes.Num() >= 3
		&& ReportBytes[0] == 0xEF
		&& ReportBytes[1] == 0xBB
		&& ReportBytes[2] == 0xBF);

	FString ReportJson;
	TestTrue(TEXT("Load execute-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
	TestTrue(TEXT("Parse execute-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
	if (RootObject.IsValid())
	{
		TestEqual(TEXT("Execute data path report keeps requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")), Request.OutputPath);
		TestEqual(TEXT("Execute data path report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
	}

	IFileManager::Get().Delete(*ReportPath);
	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportDiagnosticReportFailurePathTest,
	"Design2UMG.Import.ImportDiagnosticReportFailurePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportDiagnosticReportFailurePathTest::RunTest(const FString& Parameters)
{
	const FString ReportDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), TEXT("ImportReports"));
	TArray<FString> ReportsBefore;
	IFileManager::Get().FindFilesRecursive(ReportsBefore, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	TSet<FString> ExistingReports;
	for (const FString ExistingReport : ReportsBefore)
	{
		ExistingReports.Add(ExistingReport);
	}

	FDesignDocument Document;
	Document.Version = TEXT("pen-fixture-failure-v1");
	Document.Metadata.Add(
		TEXT("design.sourceFilePath"),
		FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Design2UMG"), FString::Printf(TEXT("fixture_%s.pen"), *FGuid::NewGuid().ToString(EGuidFormats::Digits))));

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImportReportFailureFixture");
	Data.RootWidget.Type = TEXT("CanvasPanel");
	Data.RootWidget.Id = TEXT("root");
	Data.RootWidget.Name = TEXT("Root");
	Data.RootWidget.Bounds = {0.0f, 0.0f, 120.0f, 60.0f};

	FDesignImportRequest Request;
	Request.OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImportReportFailure");
	Request.bUsePreview = false;
	Request.bResolveImages = false;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintAndPng;
	Request.DiagnosticDocument = Document;

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);
	TestTrue(TEXT("BlueprintAndPng path succeeds under NullRHI"), Result.bSucceeded);
	TestTrue(TEXT("BlueprintAndPng path saves generated asset path"), !Result.GeneratedAssetPath.IsEmpty());
	TestTrue(TEXT("BlueprintAndPng path saves rendered png path"), !Result.RenderPngPath.IsEmpty());

	TArray<FString> ReportsAfter;
	IFileManager::Get().FindFilesRecursive(ReportsAfter, *ReportDir, TEXT("*.import_report.json"), true, false, false);
	FString ReportPath;
	for (const FString& Candidate : ReportsAfter)
	{
		if (!ExistingReports.Contains(Candidate))
		{
			ReportPath = Candidate;
			break;
		}
	}

	TestTrue(TEXT("Failure path still writes report"), !ReportPath.IsEmpty() && FPaths::FileExists(ReportPath));
	if (!ReportPath.IsEmpty() && FPaths::FileExists(ReportPath))
	{
		FString ReportJson;
		TestTrue(TEXT("Load failure-path report json"), FFileHelper::LoadFileToString(ReportJson, *ReportPath));
		TSharedPtr<FJsonObject> RootObject;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReportJson);
		TestTrue(TEXT("Parse failure-path report json"), FJsonSerializer::Deserialize(Reader, RootObject) && RootObject.IsValid());
		if (RootObject.IsValid())
		{
			TestEqual(TEXT("BlueprintAndPng report keeps generated asset path"), RootObject->GetStringField(TEXT("generatedAssetPath")), Result.GeneratedAssetPath);
			TestEqual(TEXT("BlueprintAndPng report keeps rendered png path"), RootObject->GetStringField(TEXT("renderPngPath")), Result.RenderPngPath);
			TestTrue(TEXT("BlueprintAndPng report still records requested output"), RootObject->GetStringField(TEXT("requestedOutputPath")) == Request.OutputPath);
		}
		IFileManager::Get().Delete(*ReportPath);
	}

	DeleteGeneratedAssetsAtPath(Request.OutputPath);
	return true;
}
#endif
