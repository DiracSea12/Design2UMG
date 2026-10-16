// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Misc/AutomationTest.h"

#if WITH_AUTOMATION_WORKER
#include "DesignImportPlanning.h"
#include "DesignImportDiagnosticReport.h"
#include "Design2UMGSettings.h"
#include "DesignSchema.h"
#include "UMGJsonSerializer.h"
#include "Tests/Design2UMGReimportTestFixtures.h"
#include "Tests/Design2UMGFileFailureFixture.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/ScopeExit.h"
#include "Misc/SecureHash.h"
#include "Modules/ModuleManager.h"
#include "UMGWidgetTypes.h"
#include "UObject/Package.h"

namespace
{
	TArray<uint8> MakeResultFlowPng(const uint8 FirstRed = 255)
	{
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> Wrapper = Module.CreateImageWrapper(EImageFormat::PNG);
		const uint8 Pixels[16] = { 0, 0, FirstRed, 255, 0, 255, 0, 255, 255, 0, 0, 255, 255, 255, 255, 255 };
		if (!Wrapper.IsValid() || !Wrapper->SetRaw(Pixels, sizeof(Pixels), 2, 2, ERGBFormat::BGRA, 8)) return {};
		const TArray64<uint8> Bytes = Wrapper->GetCompressed();
		TArray<uint8> Result;
		Result.Append(Bytes.GetData(), static_cast<int32>(Bytes.Num()));
		return Result;
	}

	FUMGWidgetData MakeResultFlowImage(const FString& Path)
	{
		FUMGWidgetData Data;
		Data.WidgetName = TEXT("ImageResultFlow");
		Data.RootWidget.Id = TEXT("image-node");
		Data.RootWidget.Name = TEXT("Image");
		Data.RootWidget.Type = UMGWidgetTypes::Image;
		Data.RootWidget.Bounds = FDesignPixelBounds{ 0, 0, 64, 64 };
		Data.RootWidget.Brush.Image.TexturePath = Path;
		return Data;
	}

	FString HashBytes(TConstArrayView<uint8> Bytes)
	{
		uint8 Hash[FSHA1::DigestSize];
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash);
		return BytesToHex(Hash, UE_ARRAY_COUNT(Hash));
	}

	// An unrelated old texture must never substitute for the current source.
	UTexture2D* SeedUnrelatedTexture(const FString& Folder, const FString& Source, const TArray<uint8>& Bytes)
	{
		FString FullSource = FPaths::ConvertRelativePathToFull(Source);
		FPaths::NormalizeFilename(FullSource);
		FullSource.ToLowerInline();
		const FString Key = TEXT("local://") + FullSource + TEXT("#version:") + HashBytes(Bytes);
		const FTCHARToUTF8 Utf8(*Key);
		const FString Name = TEXT("T_ImageResultFlow_Image_")
			+ HashBytes(MakeArrayView(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length())).Left(10) + TEXT("_ui4");
		UPackage* Package = CreatePackage(*(Folder / TEXT("ImageResultFlow") / Name));
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
		const uint8 Pixel[4] = { 255, 255, 255, 255 };
		Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, Pixel);
		FAssetRegistryModule::AssetCreated(Texture);
		return Texture;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageResultRetryActualAttemptsTest,
	"Design2UMG.Resources.ImageResultFlow.RetryActualAttempts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageResultRetryActualAttemptsTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageResults_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString File = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	TGuardValue<FString> RestorePath(UDesign2UMGSettings::Get()->RemoteImageImportPath, Folder);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); IFileManager::Get().Delete(*File); };
	const TArray<uint8> Broken = { 1, 2, 3, 4 };
	TestTrue(TEXT("Write invalid local image"), FFileHelper::SaveArrayToFile(Broken, *File));
	FUMGWidgetData Data = MakeResultFlowImage(File);
	FDesignImageImportResult Result = DesignImportPlanning::ResolveImages(Data);
	TestEqual(TEXT("Actual decode failed"), Result.FailedCount, 1);
	Result = DesignImportPlanning::ResolveImages(Data, &Result);
	TestEqual(TEXT("Repeated failure replaces rather than duplicates"), Result.FailedCount, 1);
	TestTrue(TEXT("Write valid retry source"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(), *File));
	Result = DesignImportPlanning::ResolveImages(Data, &Result);
	TestEqual(TEXT("Actual successful retry clears failure"), Result.FailedCount, 0);
	TestEqual(TEXT("One successful import retained"), Result.ImportedCount, 1);
	TestTrue(TEXT("Source rewritten to asset"), Data.RootWidget.Brush.Image.TexturePath.GetValue().StartsWith(TEXT("/Game/")));
	for (int32 Retry = 0; Retry < 2; ++Retry)
	{
		Result = DesignImportPlanning::ResolveImages(Data, &Result);
		TestEqual(TEXT("Asset-only retry keeps earlier import fact"), Result.ImportedCount, 1);
		TestEqual(TEXT("Asset-only retry does not duplicate facts"), Result.Diagnostics.Num(), 1);
	}
	TGuardValue<bool> RestoreEnabled(UDesign2UMGSettings::Get()->bEnableRemoteImageImport, true);
	Data = MakeResultFlowImage(TEXT("https://@blocked.invalid/disabled_retry.png"));
	Result = DesignImportPlanning::ResolveImages(Data);
	TestEqual(TEXT("Remote validation actually failed"), Result.FailedCount, 1);
	UDesign2UMGSettings::Get()->bEnableRemoteImageImport = false;
	for (int32 Retry = 0; Retry < 2; ++Retry)
	{
		Result = DesignImportPlanning::ResolveImages(Data, &Result);
		TestEqual(TEXT("Disabled import did not repair earlier failure"), Result.FailedCount, 1);
		TestEqual(TEXT("Repeated skip does not duplicate facts"), Result.SkippedCount, 1);
		TestEqual(TEXT("Skip and unattempted failure both remain"), Result.Diagnostics.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageResultEditedSourceReconciliationTest,
	"Design2UMG.Resources.ImageResultFlow.EditedSourceReconciliation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageResultEditedSourceReconciliationTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageEditedSource_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString FailedFile = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	const FString GoodFile = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	TGuardValue<FString> RestorePath(UDesign2UMGSettings::Get()->RemoteImageImportPath, Folder);
	ON_SCOPE_EXIT
	{
		FixtureWipeOutput(Folder);
		IFileManager::Get().Delete(*FailedFile);
		IFileManager::Get().Delete(*GoodFile);
	};
	TestTrue(TEXT("Write invalid source A"), FFileHelper::SaveArrayToFile(TArray<uint8>{ 1, 2, 3, 4 }, *FailedFile));
	TestTrue(TEXT("Write valid source B"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(), *GoodFile));

	FUMGWidgetData Original = MakeResultFlowImage(FailedFile);
	const FDesignImageImportResult Initial = DesignImportPlanning::ResolveImages(Original);
	TestEqual(TEXT("Source A fails"), Initial.FailedCount, 1);

	FPreviewRefreshInput RefreshInput;
	RefreshInput.OriginalData = MakeShared<FUMGWidgetData>(Original);
	RefreshInput.InitialImageResult = Initial;
	RefreshInput.bResolveImages = false;
	RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Original);
	FPreviewRefreshResult Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	TestTrue(TEXT("Unchanged source refresh succeeds"), Refresh.bSucceeded);
	TestEqual(TEXT("Same source without retry keeps failure"), Refresh.ImageResult.FailedCount, 1);
	TestTrue(TEXT("Unretried preview summary keeps its image import path"),
		Refresh.ImageResult.DisplaySummary.Contains(Folder));

	FUMGWidgetData Edited = Original;
	Edited.RootWidget.Brush.Image.TexturePath = GoodFile;
	RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Edited);
	Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	TestTrue(TEXT("Changed source refresh succeeds"), Refresh.bSucceeded);
	TestEqual(TEXT("Changing source retires A before B is attempted"), Refresh.ImageResult.FailedCount, 0);
	TestEqual(TEXT("No stale source facts remain"), Refresh.ImageResult.Diagnostics.Num(), 0);
	RefreshInput.bResolveImages = true;
	Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	TestEqual(TEXT("Successful B is the only current fact"), Refresh.ImageResult.ImportedCount, 1);
	TestEqual(TEXT("Old A failure stays retired"), Refresh.ImageResult.FailedCount, 0);
	TestEqual(TEXT("B is not double counted"), Refresh.ImageResult.Diagnostics.Num(), 1);

	Edited.RootWidget.Brush.Image.TexturePath = TEXT("/Game/Design2UMG/SomeOtherTexture.SomeOtherTexture");
	RefreshInput.bResolveImages = false;
	RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Edited);
	Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	TestEqual(TEXT("Replacing A with a different asset retires its failure"), Refresh.ImageResult.FailedCount, 0);

	{
		TGuardValue<bool> RestoreEnabled(UDesign2UMGSettings::Get()->bEnableRemoteImageImport, false);
		Edited.RootWidget.Brush.Image.TexturePath = TEXT("https://@blocked.invalid/new_source.png");
		RefreshInput.bResolveImages = true;
		RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Edited);
		Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
		TestEqual(TEXT("Disabled new source does not retain A's failure"), Refresh.ImageResult.FailedCount, 0);
		TestEqual(TEXT("Disabled new source is recorded once as skipped"), Refresh.ImageResult.SkippedCount, 1);
	}

	Edited.RootWidget.Brush.Image.TexturePath.Reset();
	RefreshInput.bResolveImages = false;
	RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Edited);
	Refresh = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	TestEqual(TEXT("Removing image source retires A"), Refresh.ImageResult.FailedCount, 0);

	FUMGWidgetData WithTwoSources = MakeResultFlowImage(FailedFile);
	FUMGWidgetNode OtherImage = WithTwoSources.RootWidget;
	OtherImage.Name = TEXT("Second image");
	OtherImage.Brush.Image.TexturePath = GoodFile;
	WithTwoSources.RootWidget.Children.Add(OtherImage);
	FDesignImageImportResult TwoSourceResult = DesignImportPlanning::ResolveImages(WithTwoSources);
	TestEqual(TEXT("One node identity can hold a failed and successful source"), TwoSourceResult.FailedCount, 1);
	TestEqual(TEXT("The other source succeeds"), TwoSourceResult.ImportedCount + TwoSourceResult.ReusedCount, 1);
	TwoSourceResult = DesignImportPlanning::ResolveImages(WithTwoSources, &TwoSourceResult);
	TestEqual(TEXT("Unretried A survives B's asset-only pass"), TwoSourceResult.FailedCount, 1);
	TestEqual(TEXT("B success survives without duplication"), TwoSourceResult.ImportedCount + TwoSourceResult.ReusedCount, 1);
	TestEqual(TEXT("Distinct facts remain distinct"), TwoSourceResult.Diagnostics.Num(), 2);
	WithTwoSources.RootWidget.Brush.Image.TexturePath.Reset();
	TwoSourceResult = DesignImportPlanning::ResolveImages(WithTwoSources, &TwoSourceResult);
	TestEqual(TEXT("Removing A retires its failure even while B's asset remains"), TwoSourceResult.FailedCount, 0);
	TestEqual(TEXT("B's successful source fact stays current"), TwoSourceResult.ImportedCount + TwoSourceResult.ReusedCount, 1);
	TestEqual(TEXT("Only B's fact remains"), TwoSourceResult.Diagnostics.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageResultAssetSourceIdentityTest,
	"Design2UMG.Resources.ImageResultFlow.AssetSourceIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageResultAssetSourceIdentityTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageSourceIdentity_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString SourceA = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	const FString SourceB = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	TGuardValue<FString> RestorePath(UDesign2UMGSettings::Get()->RemoteImageImportPath, Folder);
	ON_SCOPE_EXIT
	{
		FixtureWipeOutput(Folder);
		IFileManager::Get().Delete(*SourceA);
		IFileManager::Get().Delete(*SourceB);
	};
	TestTrue(TEXT("Write source A"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(), *SourceA));
	TestTrue(TEXT("Write different source B"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(180), *SourceB));

	FUMGWidgetData Single = MakeResultFlowImage(SourceA);
	FDesignImageImportResult SingleResult = DesignImportPlanning::ResolveImages(Single);
	TestEqual(TEXT("A initially succeeds"), SingleResult.ImportedCount + SingleResult.ReusedCount, 1);
	const FString AssetA = Single.RootWidget.Brush.Image.TexturePath.Get(FString());
	TestTrue(TEXT("A became an asset path"), AssetA.StartsWith(TEXT("/Game/")));
	if (TestEqual(TEXT("A has one source diagnostic"), SingleResult.Diagnostics.Num(), 1))
	{
		TestEqual(TEXT("A diagnostic owns the asset it wrote"), SingleResult.Diagnostics[0].ResolvedAssetPath, AssetA);
	}
	SingleResult = DesignImportPlanning::ResolveImages(Single, &SingleResult);
	TestEqual(TEXT("The same resolved asset retains A's success"), SingleResult.ImportedCount + SingleResult.ReusedCount, 1);
	Single.RootWidget.Brush.Image.TexturePath = TEXT("/Game/Design2UMG/AnotherAsset.AnotherAsset");
	SingleResult = DesignImportPlanning::ResolveImages(Single, &SingleResult);
	TestEqual(TEXT("Replacing A with another asset retires A's success"), SingleResult.ImportedCount + SingleResult.ReusedCount, 0);
	TestEqual(TEXT("The replacement asset has no stale A diagnostic"), SingleResult.Diagnostics.Num(), 0);

	FUMGWidgetData Multiple = MakeResultFlowImage(SourceA);
	FUMGWidgetNode Second = Multiple.RootWidget;
	Second.Name = TEXT("Second image");
	Second.Brush.Image.TexturePath = SourceB;
	Multiple.RootWidget.Children.Add(Second);
	FDesignImageImportResult MultipleResult = DesignImportPlanning::ResolveImages(Multiple);
	TestEqual(TEXT("Both sources succeed"), MultipleResult.ImportedCount + MultipleResult.ReusedCount, 2);
	const FString AssetB = Multiple.RootWidget.Children[0].Brush.Image.TexturePath.Get(FString());
	TestNotEqual(TEXT("The two sources produced different assets"), Multiple.RootWidget.Brush.Image.TexturePath.Get(FString()), AssetB);
	for (const FDesignImageDiagnostic& Diagnostic : MultipleResult.Diagnostics)
	{
		if (Diagnostic.Resource == SourceA)
		{
			TestEqual(TEXT("A fact binds to A asset"), Diagnostic.ResolvedAssetPath,
				Multiple.RootWidget.Brush.Image.TexturePath.Get(FString()));
		}
		else if (Diagnostic.Resource == SourceB)
		{
			TestEqual(TEXT("B fact binds to B asset"), Diagnostic.ResolvedAssetPath, AssetB);
		}
	}
	Multiple.RootWidget.Brush.Image.TexturePath.Reset();
	MultipleResult = DesignImportPlanning::ResolveImages(Multiple, &MultipleResult);
	TestEqual(TEXT("Removing A leaves only B's success"), MultipleResult.ImportedCount + MultipleResult.ReusedCount, 1);
	TestEqual(TEXT("Only B's diagnostic remains"), MultipleResult.Diagnostics.Num(), 1);
	if (MultipleResult.Diagnostics.Num() == 1)
	{
		TestEqual(TEXT("The retained source is B"), MultipleResult.Diagnostics[0].Resource, SourceB);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageResultUnrelatedTextureFailureIdentityTest,
	"Design2UMG.Resources.ImageResultFlow.UnrelatedTextureFailureIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageResultUnrelatedTextureFailureIdentityTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageRefresh_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString File = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	TGuardValue<FString> RestorePath(UDesign2UMGSettings::Get()->RemoteImageImportPath, Folder);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); IFileManager::Get().Delete(*File); };
	const TArray<uint8> Broken = { 1, 2, 3, 4 };
	TestTrue(TEXT("Write invalid refresh source"), FFileHelper::SaveArrayToFile(Broken, *File));
	UTexture2D* Texture = SeedUnrelatedTexture(Folder, File, Broken);
	if (!TestNotNull(TEXT("Existing per-widget texture"), Texture)) return false;
	FUMGWidgetData Data = MakeResultFlowImage(File);
	FDesignImageImportResult Result = DesignImportPlanning::ResolveImages(Data);
	TestEqual(TEXT("Source decode failure recorded"), Result.FailedCount, 1);
	TestEqual(TEXT("Unrelated old texture is not reused"), Result.ReusedCount, 0);
	const FDesignImageDiagnostic* Failure = Result.Diagnostics.FindByPredicate([](const FDesignImageDiagnostic& D)
	{
		return D.Outcome == EDesignImageDiagnosticOutcome::Failed;
	});
	if (TestNotNull(TEXT("Structured source failure"), Failure))
	{
		TestEqual(TEXT("Failure identifies actual node"), Failure->NodeId, Data.RootWidget.Id);
		TestEqual(TEXT("Failure identifies failed source file"), Failure->Resource, File);
		TestTrue(TEXT("Failure does not claim an unrelated retained asset"), Failure->ResolvedAssetPath.IsEmpty());
	}
	for (int32 Retry = 0; Retry < 2; ++Retry)
	{
		Result = DesignImportPlanning::ResolveImages(Data, &Result);
		TestEqual(TEXT("Failed source remains reported across retries"), Result.FailedCount, 1);
		TestEqual(TEXT("Retries never reuse unrelated textures"), Result.ReusedCount, 0);
		TestEqual(TEXT("The source failure is not duplicated"), Result.Diagnostics.Num(), 1);
	}
	FPreviewApprovedSnapshot Approved;
	Approved.Data = Data;
	Approved.ImageResult = Result;
	FArtifactGenerationRequest Generation;
	Generation.OutputPath = Folder / TEXT("Widgets");
	Generation.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Generation.bResolveImages = true;
	const FArtifactGenerationResult Generated = DesignImportPipeline::GenerateArtifacts(Approved, Generation);
	TestTrue(TEXT("Preview-approved import remains usable"), Generated.bSucceeded);
	TestEqual(TEXT("Preview retry retains source degradation"), Generated.Commit.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestTrue(TEXT("Repair source for an actual retry"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(), *File));
	Data.RootWidget.Brush.Image.TexturePath = File;
	Result = DesignImportPlanning::ResolveImages(Data, &Result);
	TestEqual(TEXT("Actual source retry clears source failure"), Result.FailedCount, 0);
	TestEqual(TEXT("Actual source retry replaces old reuse fact"), Result.ReusedCount, 0);
	TestEqual(TEXT("Actual source retry records new import"), Result.ImportedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageResultSafeReimportReportTest,
	"Design2UMG.Resources.ImageResultFlow.SafeReimportReport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageResultSafeReimportReportTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageReimport_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); };
	TGuardValue<bool> RestoreEnabled(UDesign2UMGSettings::Get()->bEnableRemoteImageImport, true);
	const FString Json = MakeFixtureDoc(MakeFixtureButton(TEXT("mapped"), TEXT("Mapped"), 100, 0, TEXT("Label"))
		+ TEXT(",{\"id\":\"image\",\"name\":\"Image\",\"type\":\"RECTANGLE\","
		"\"absoluteBoundingBox\":{\"x\":0,\"y\":0,\"width\":64,\"height\":64}}"));
	UWidgetBlueprint* Blueprint = FixtureImport(Json, FixtureMappedClassPath, Folder);
	if (!TestNotNull(TEXT("Initial asset imported"), Blueprint)) return false;
	FDesignDocument Document;
	if (!TestTrue(TEXT("Parse reimport source"), FFigmaParser::ParseDocument(Json, Document))) return false;
	const FString FailedUrl = TEXT("https://@blocked.invalid/reimport_image.png");
	TFunction<void(const TSharedPtr<FDesignNode>&)> AddImage = [&](const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Id == TEXT("image"))
		{
			FDesignFill Fill;
			Fill.Type = EDesignFillType::Image;
			Fill.Image.Url = FailedUrl;
			Node->Visual.Fills.Add(Fill);
		}
		for (const auto& Child : Node->Children) AddImage(Child);
	};
	for (const auto& Node : Document.Children) AddImage(Node);
	const FString ReportDir = FPaths::ProjectSavedDir() / TEXT("Design2UMG/ImportReports");
	const auto Reports = [&]()
	{
		TArray<FString> Paths;
		IFileManager::Get().FindFilesRecursive(Paths, *ReportDir, TEXT("*.import_report.json"), true, false);
		return TSet<FString>(Paths);
	};
	const auto CheckImageReport = [&](const TSet<FString>& Before, const TCHAR* Status, int32 FailedCount)
	{
		for (const FString& Path : Reports())
		{
			if (Before.Contains(Path)) continue;
			FString Text;
			TSharedPtr<FJsonObject> Root;
			if (!TestTrue(TEXT("Read emitted report"), FFileHelper::LoadFileToString(Text, *Path)
				&& FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())) return;
			const TSharedPtr<FJsonObject> Resources = Root->GetObjectField(TEXT("resources"));
			const TSharedPtr<FJsonObject> Images = Resources->GetObjectField(TEXT("imageImport"));
			TestEqual(TEXT("Report gives actual analysis status"), Images->GetStringField(TEXT("status")), FString(Status));
			TestEqual(TEXT("Report preserves failure count"), static_cast<int32>(Images->GetNumberField(TEXT("failedCount"))), FailedCount);
			TestEqual(TEXT("Report preserves failure details"), Resources->GetArrayField(TEXT("imageFailures")).Num(), FailedCount);
			return;
		}
		AddError(TEXT("Expected reimport report was not written"));
	};
	FDesignReimportRequest Request;
	Request.TargetAssetPath = Blueprint->GetPathName();
	Request.SettingsSnapshot = MakeFixtureSnapshot(FixtureMappedClassPath);
	TSet<FString> Before = Reports();
	const FDesignReimportResult Result = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("Reimport continues for an optional failed image"), Result.bSucceeded);
	TestTrue(TEXT("Image failure degrades reimport"), Result.bHasDegradation);
	TestTrue(TEXT("Reimport owns its executed image analysis"), Result.ImageResult.bWasExecuted);
	TestEqual(TEXT("Reimport returns its image failure"), Result.ImageResult.FailedCount, 1);
	TestEqual(TEXT("Image failure degrades saved commit"), Result.Commit.Status, EDesign2UMGCommitStatus::SucceededWithDegradation);
	CheckImageReport(Before, TEXT("failed"), 1);
	{
		Design2UMGEditorTests::FScopedFileFailures Failures;
		Failures.RejectWrite = [](const FString& Path) { return Path.EndsWith(TEXT(".d2u.json.tmp")); };
		AddExpectedError(TEXT("GenerationManifest: failed to write temp manifest"), EAutomationExpectedErrorFlags::Contains, 1);
		Before = Reports();
		const FDesignReimportResult FailedSave = DesignImportPipeline::Reimport(Document, Request);
		TestEqual(TEXT("Metadata failure remains the commit outcome"), FailedSave.Commit.Status, EDesign2UMGCommitStatus::CommittedWithMetadataFailure);
		TestEqual(TEXT("Failed save retains image analysis"), FailedSave.ImageResult.FailedCount, 1);
		CheckImageReport(Before, TEXT("failed"), 1);
	}
	Before = Reports();
	Request.TargetAssetPath = Folder / TEXT("Missing.Missing");
	const FDesignReimportResult Conflict = DesignImportPipeline::Reimport(Document, Request);
	TestTrue(TEXT("Missing target reports conflict"), Conflict.bFullConflict);
	TestEqual(TEXT("Conflict retains image analysis"), Conflict.ImageResult.FailedCount, 1);
	CheckImageReport(Before, TEXT("failed"), 1);
	Before = Reports();
	FUMGWidgetData UnanalyzedData;
	TestTrue(TEXT("Report without image analysis written"), DesignImportDiagnosticReport::WriteReimport(Document, UnanalyzedData, Request.TargetAssetPath, {}));
	CheckImageReport(Before, TEXT("not_run"), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImageImportSettingsSnapshotTest,
	"Design2UMG.Resources.ImageResultFlow.SettingsSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImageImportSettingsSnapshotTest::RunTest(const FString&)
{
	using namespace Design2UMGReimportFixtures;
	UDesign2UMGSettings* Settings = UDesign2UMGSettings::Get();
	const FString Folder = TEXT("/Game/Design2UMG/_TestOutput/ImageSettings_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString File = FPaths::ProjectSavedDir() / TEXT("Design2UMG") / (FGuid::NewGuid().ToString() + TEXT(".png"));
	TGuardValue<FString> RestorePath(Settings->RemoteImageImportPath, Folder);
	ON_SCOPE_EXIT { FixtureWipeOutput(Folder); IFileManager::Get().Delete(*File); };
	if (!TestTrue(TEXT("Write source image"), FFileHelper::SaveArrayToFile(MakeResultFlowPng(), *File))) return false;
	const FDesign2UMGImportSettingsSnapshot Snapshot = FDesign2UMGImportSettingsSnapshot::FromSettings(Settings);
	Settings->RemoteImageImportPath = Folder / TEXT("Changed");
	FUMGWidgetData Data = MakeResultFlowImage(File);
	FImportAnalysisInput Input;
	Input.SettingsSnapshot = &Snapshot;
	const FImportInPlaceAnalysisResult Result = DesignImportPlanning::AnalyzeImport(Data, Input);
	TestEqual(TEXT("Image analysis uses the captured import path"), Result.ImageResult.ImportPath, Folder);
	TestTrue(TEXT("Generated image stays under the captured path"), Data.RootWidget.Brush.Image.TexturePath.Get(FString()).StartsWith(Folder / TEXT("_Shared")));
	return true;
}

#endif
