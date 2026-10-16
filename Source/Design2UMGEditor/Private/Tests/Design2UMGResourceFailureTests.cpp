// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGAutomationTestAdapter.h"
#include "Misc/Build.h"

#if WITH_AUTOMATION_WORKER

#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "DesignImportPipeline.h"
#include "DesignImportPlanning.h"
#include "Design2UMGSettings.h"
#include "DesignImageResourceLimits.h"
#include "FigmaImageExporter.h"
#include "FigmaHttpClient.h"
#include "DesignSchema.h"
#include "FigmaParser.h"
#include "PreBuildResourceNormalizer.h"
#include "UMGSchema.h"
#include "UMGJsonSerializer.h"
#include "UMGWidgetTypes.h"

#include "Tests/Design2UMGHttpTestTransport.h"
#include "Tests/Design2UMGTestNodes.h"
#include "Tests/Design2UMGTestAssetCleanup.h"

using Design2UMGEditorTests::FScopedHttpTestTransport;

namespace
{
	// ---------------------------------------------------------------------------
	// Remote-image failures are returned as typed diagnostics.
	// ---------------------------------------------------------------------------

	struct FScopedRemoteImageImportSettings
	{
		FScopedRemoteImageImportSettings()
		{
			Settings = UDesign2UMGSettings::Get();
			if (!Settings)
			{
				return;
			}
			bSavedEnabled = Settings->bEnableRemoteImageImport;
			Settings->bEnableRemoteImageImport = true;
		}

		~FScopedRemoteImageImportSettings()
		{
			if (Settings)
			{
				Settings->bEnableRemoteImageImport = bSavedEnabled;
			}
		}

		UDesign2UMGSettings* Settings = nullptr;
		bool bSavedEnabled = false;
	};

	FUMGWidgetNode MakeResourceFailureCanvas(const FString& Name)
	{
		FUMGWidgetNode Root;
		Root.Type = UMGWidgetTypes::CanvasPanel;
		Root.Id = Name;
		Root.Name = Name;
		Root.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 320.0f, 240.0f };
		return Root;
	}

	// An https URL whose authority contains '@' is rejected by
	// FImagePayloadIO::ValidateRemoteUrl before any socket is opened, so the failure is
	// deterministic and never touches the network.
	const TCHAR* GUnresolvableRemoteUrl = TEXT("https://@blocked.invalid/resource_failure_missing.png");

	FUMGWidgetNode MakeResourceFailureImageNode(const FString& Id, const FString& TexturePath)
	{
		FUMGWidgetNode Node;
		Node.Type = UMGWidgetTypes::Image;
		Node.Id = Id;
		Node.Name = Id;
		Node.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 64.0f, 64.0f };
		Node.Brush.Image.TexturePath = TexturePath;
		return Node;
	}

	// ---------------------------------------------------------------------------
	// b: Figma image export must report a missing meta.images as a failure.
	// ---------------------------------------------------------------------------

	TSharedRef<FDesignDocument> MakeDocumentWithImageRef(const FString& Ref)
	{
		const TSharedRef<FDesignDocument> Document = MakeShared<FDesignDocument>();
		TSharedPtr<FDesignNode> Node = Design2UMGEditorTests::MakeNode(
			TEXT("resource_failure_node"), TEXT("ResourceFailure_Image"), EDesignNodeType::Rectangle, 64.0f, 64.0f);
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Image;
		Fill.Image.Url = Ref;
		Node->Visual.Fills.Add(Fill);
		Document->Children.Add(Node);
		return Document;
	}

	/**
	 * A vector/raster fallback ref: the node owns an explicit alternative rendering path
	 * (its own vector geometry), so an unresolved ref here is an allowed substitute.
	 */
	TSharedRef<FDesignDocument> MakeDocumentWithVectorRasterFallbackRef(const FString& Ref)
	{
		const TSharedRef<FDesignDocument> Document = MakeShared<FDesignDocument>();
		TSharedPtr<FDesignNode> Node = Design2UMGEditorTests::MakeNode(
			TEXT("resource_failure_vector"), TEXT("ResourceFailure_Vector"), EDesignNodeType::Path, 64.0f, 64.0f);
		Node->Visual.VectorRasterFallbackImageRef = Ref;
		Node->Shape.Geometry = TEXT("M0 0L64 0L64 64Z");
		FDesignFill Fill;
		Fill.Type = EDesignFillType::Color;
		Fill.SolidColor = TEXT("#FFFFFF");
		Node->Visual.Fills.Add(Fill);
		Document->Children.Add(Node);
		return Document;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureRemoteImageFailureEntersTypedResultTest,
	"Design2UMG.Resources.Failure.RemoteImageFailureEntersTypedResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureRemoteImageFailureEntersTypedResultTest::RunTest(const FString& Parameters)
{
	FScopedRemoteImageImportSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings are unavailable"));
		return false;
	}

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ResourceFailure_RemoteFailure");
	Data.RootWidget = MakeResourceFailureImageNode(TEXT("ResourceFailure_RemoteImage"), GUnresolvableRemoteUrl);
	TestEqual(TEXT("Input data starts without warnings"), Data.Warnings.Num(), 0);

	const FDesignImageImportResult ImageResult = DesignImportPlanning::ResolveImages(Data);
	const FString& Report = ImageResult.DisplaySummary;

	TestTrue(TEXT("Failure report is produced"), !Report.IsEmpty());
	TestTrue(TEXT("Report records one failed image"), Report.Contains(TEXT("failed=1")));
	TestTrue(TEXT("Report names the failed URL"), Report.Contains(TEXT("resource_failure_missing.png")));

	TestEqual(TEXT("The typed result records one image failure"), ImageResult.FailedCount, 1);
	TestEqual(TEXT("Image failures do not enter general warnings"), Data.Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureNoRemoteImageKeepsDataCleanTest,
	"Design2UMG.Resources.Failure.NoRemoteImageKeepsDataClean",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureNoRemoteImageKeepsDataCleanTest::RunTest(const FString& Parameters)
{
	FScopedRemoteImageImportSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings are unavailable"));
		return false;
	}

	// Control for the failure test above: a document that needs no remote images must
	// not gain a degradation warning.
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ResourceFailure_NoRemote");
	Data.RootWidget = MakeResourceFailureImageNode(TEXT("ResourceFailure_Local"), TEXT("/Game/Design2UMG/Test/T_ResourceFailure.T_ResourceFailure"));

	const FDesignImageImportResult ImageResult = DesignImportPlanning::ResolveImages(Data);
	const FString& Report = ImageResult.DisplaySummary;

	TestTrue(TEXT("A summary is still produced for a clean run"), !Report.IsEmpty());
	TestTrue(TEXT("Clean run reports zero failures"), Report.Contains(TEXT("failed=0")));
	TestEqual(TEXT("No warning is added when nothing failed"), Data.Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureImageResolutionFailureEntersTypedResultTest,
	"Design2UMG.Resources.Failure.ImageResolutionFailureEntersTypedResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureImageResolutionFailureEntersTypedResultTest::RunTest(const FString& Parameters)
{
	FDesignImageImportResult ImageResult;
	DesignImportPlanning::RecordImageResolutionFailure(ImageResult);

	TestEqual(TEXT("The failure is recorded exactly once"), ImageResult.FailedCount, 1);
	if (TestEqual(TEXT("The diagnostic is recorded exactly once"), ImageResult.Diagnostics.Num(), 1))
	{
		TestEqual(
			TEXT("The diagnostic category identifies an unresolved reference"),
			static_cast<uint8>(ImageResult.Diagnostics[0].Category),
			static_cast<uint8>(EDesignImageDiagnosticCategory::UnresolvedReference));
	}

	DesignImportPlanning::RecordImageResolutionFailure(ImageResult);
	TestEqual(TEXT("Repeated recording does not duplicate the diagnostic"), ImageResult.Diagnostics.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureRetryPreservesSourceFailuresTest,
	"Design2UMG.Resources.Failure.RetryPreservesSourceFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureRetryPreservesSourceFailuresTest::RunTest(const FString& Parameters)
{
	FScopedRemoteImageImportSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings unavailable"));
		return false;
	}

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ResourceFailure_Retry");
	Data.RootWidget = MakeResourceFailureImageNode(
		TEXT("ResourceFailure_RemoteImage"),
		GUnresolvableRemoteUrl);
	const FString SummaryLikeWarning = TEXT("Remote image import: an unrelated author warning");
	const FString UnrelatedWarning = TEXT("Design2UMG: font family 'NotARealFont' was substituted");
	Data.Warnings = { SummaryLikeWarning, UnrelatedWarning };

	FDesignImageImportResult ImageResult;
	DesignImportPlanning::RecordImageResolutionFailure(ImageResult);
	FDesignImageDiagnostic DownloadFailure;
	DownloadFailure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	DownloadFailure.Category = EDesignImageDiagnosticCategory::Download;
	DownloadFailure.NodeId = Data.RootWidget.Id;
	DownloadFailure.NodeName = Data.RootWidget.Name;
	DownloadFailure.Resource = GUnresolvableRemoteUrl;
	DownloadFailure.Reason = TEXT("Previous download failed");
	DownloadFailure.Message = TEXT("Previous remote image download failed");
	ImageResult.AddDiagnostic(MoveTemp(DownloadFailure));
	ImageResult.RefreshDisplaySummary();
	TestEqual(TEXT("The retry starts with source and download failures"), ImageResult.FailedCount, 2);

	const FDesignImageImportResult PreviousImageResult = ImageResult;
	ScopedSettings.Settings->bEnableRemoteImageImport = false;
	ImageResult = DesignImportPlanning::ResolveImages(Data, &PreviousImageResult);
	TestEqual(TEXT("A skipped retry keeps the same source failure"), ImageResult.FailedCount, 2);
	TestEqual(TEXT("The skipped source is recorded once"), ImageResult.SkippedCount, 1);
	TestEqual(TEXT("Global, skipped, and untried failure facts remain"), ImageResult.Diagnostics.Num(), 3);

	Data.RootWidget.Brush.Image.TexturePath = TEXT("/Game/Design2UMG/Test/T_ResourceFailure.T_ResourceFailure");
	const FDesignImageImportResult BeforeSourceChange = ImageResult;
	ImageResult = DesignImportPlanning::ResolveImages(Data, &BeforeSourceChange);
	TestEqual(TEXT("Replacing the URL source retires its old failure"), ImageResult.FailedCount, 1);
	if (TestEqual(TEXT("Only the independent source-resolution failure remains"), ImageResult.Diagnostics.Num(), 1))
	{
		TestEqual(
			TEXT("The remaining diagnostic is the source-resolution failure"),
			static_cast<uint8>(ImageResult.Diagnostics[0].Category),
			static_cast<uint8>(EDesignImageDiagnosticCategory::UnresolvedReference));
	}
	TestEqual(TEXT("The retry keeps unrelated warnings"), Data.Warnings.Num(), 2);
	TestTrue(TEXT("Summary-like unrelated warning is not parsed or removed"), Data.Warnings.Contains(SummaryLikeWarning));
	TestTrue(TEXT("General warning remains"), Data.Warnings.Contains(UnrelatedWarning));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureDirectExecuteDegradesCommitTest,
	"Design2UMG.Resources.Failure.DirectExecuteDegradesCommit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureDirectExecuteDegradesCommitTest::RunTest(const FString& Parameters)
{
	FScopedRemoteImageImportSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings unavailable"));
		return false;
	}

	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImageFailureDirectExecute_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputPath); };

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImageFailureDirectExecute");
	Data.RootWidget = MakeResourceFailureCanvas(TEXT("ImageFailureDirectExecuteRoot"));
	Data.RootWidget.Children.Add(MakeResourceFailureImageNode(
		TEXT("ImageFailureDirectExecuteImage"),
		GUnresolvableRemoteUrl));

	FDesignImportRequest Request;
	Request.OutputPath = OutputPath;
	Request.bUsePreview = false;
	Request.bResolveImages = true;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	const FDesignImportExecutionResult Result = DesignImportPipeline::Execute(Data, Request);

	TestTrue(TEXT("Direct execution still produces the usable asset"), Result.bSucceeded);
	TestEqual(
		TEXT("A real image failure degrades the direct execution commit"),
		Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureApprovedPreviewGenerationPreservesSourceFailureTest,
	"Design2UMG.Resources.Failure.ApprovedPreviewGenerationPreservesSourceFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureApprovedPreviewGenerationPreservesSourceFailureTest::RunTest(const FString& Parameters)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImageFailurePreviewGenerate_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputPath); };

	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ImageFailurePreviewGenerate");
	Data.RootWidget = MakeResourceFailureImageNode(
		TEXT("ImageFailurePreviewLocal"),
		TEXT("/Game/Design2UMG/Test/T_ResourceFailure.T_ResourceFailure"));

	FDesignImageImportResult InitialImageResult;
	DesignImportPlanning::RecordImageResolutionFailure(InitialImageResult);
	FPreviewRefreshInput RefreshInput;
	RefreshInput.RequestId = 42;
	RefreshInput.OriginalData = MakeShared<FUMGWidgetData>(Data);
	RefreshInput.EditedJson = FUMGJsonSerializer::Serialize(Data);
	RefreshInput.InitialImageResult = InitialImageResult;
	RefreshInput.bResolveImages = false;
	const FPreviewRefreshResult RefreshResult = DesignImportPlanning::AnalyzePreviewRefreshInput(RefreshInput);
	if (!TestTrue(TEXT("Preview refresh succeeds"), RefreshResult.bSucceeded))
	{
		return false;
	}
	TestEqual(TEXT("Preview refresh keeps the unresolved source failure"), RefreshResult.ImageResult.FailedCount, 1);

	FPreviewApprovedSnapshot ApprovedSnapshot;
	ApprovedSnapshot.RequestId = RefreshResult.RequestId;
	ApprovedSnapshot.EditedJson = RefreshResult.EditedJson;
	ApprovedSnapshot.Data = RefreshResult.Data;
	ApprovedSnapshot.ImageResult = RefreshResult.ImageResult;
	const FString ApprovedDataBeforeGeneration = FUMGJsonSerializer::Serialize(ApprovedSnapshot.Data);

	FArtifactGenerationRequest Request;
	Request.OutputPath = OutputPath;
	Request.bResolveImages = true;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	const FArtifactGenerationResult Result = DesignImportPipeline::GenerateArtifacts(ApprovedSnapshot, Request);

	TestTrue(TEXT("Approved preview generation still produces the usable asset"), Result.bSucceeded);
	TestEqual(
		TEXT("The unresolved source failure survives URL resolution and degrades the commit"),
		Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	TestTrue(TEXT("The degradation exposes a human-facing image summary"),
		Result.Warnings.ContainsByPredicate([](const FString& Warning)
		{
			return Warning.Contains(TEXT("unresolved_reference=1"));
		}));
	TestEqual(
		TEXT("Generation keeps the approved preview snapshot immutable"),
		FUMGJsonSerializer::Serialize(ApprovedSnapshot.Data),
		ApprovedDataBeforeGeneration);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureStructuredRuntimeResultIsAuthoritativeTest,
	"Design2UMG.Resources.Failure.StructuredRuntimeResultIsAuthoritative",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureStructuredRuntimeResultIsAuthoritativeTest::RunTest(const FString& Parameters)
{
	const FString OutputPath = TEXT("/Game/Design2UMG/_TestOutput/ImageFailureRuntimeAuthority_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(OutputPath); };

	FPreviewApprovedSnapshot ApprovedSnapshot;
	ApprovedSnapshot.RequestId = 77;
	ApprovedSnapshot.Data.WidgetName = TEXT("WBP_ImageFailureRuntimeAuthority");
	ApprovedSnapshot.Data.RootWidget = MakeResourceFailureCanvas(TEXT("ImageFailureRuntimeAuthorityRoot"));

	FDesignImageImportResult RuntimeImageResult;
	FDesignImageDiagnostic Failure;
	Failure.Outcome = EDesignImageDiagnosticOutcome::Failed;
	Failure.Category = EDesignImageDiagnosticCategory::Download;
	Failure.Resource = TEXT("https://example.invalid/runtime.png");
	Failure.Reason = TEXT("Runtime download failed");
	RuntimeImageResult.AddDiagnostic(MoveTemp(Failure));
	TestTrue(TEXT("The structured result intentionally has no display text"),
		RuntimeImageResult.DisplaySummary.IsEmpty());

	FArtifactGenerationRequest Request;
	Request.OutputPath = OutputPath;
	Request.Generation.Mode = EDesignBlueprintGenerationMode::BlueprintOnly;
	Request.RuntimeImageResult = RuntimeImageResult;
	const FArtifactGenerationResult Result = DesignImportPipeline::GenerateArtifacts(ApprovedSnapshot, Request);

	TestTrue(TEXT("Generation succeeds with a structured runtime failure"), Result.bSucceeded);
	TestEqual(
		TEXT("Structured facts remain authoritative when display text is empty"),
		Result.Commit.Status,
		EDesign2UMGCommitStatus::SucceededWithDegradation);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureUrlLimitFailureClearsWithLocalImagesTest,
	"Design2UMG.Resources.Failure.UrlLimitFailureClearsWithLocalImages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureUrlLimitFailureClearsWithLocalImagesTest::RunTest(const FString& Parameters)
{
	FScopedRemoteImageImportSettings ScopedSettings;
	if (!ScopedSettings.Settings)
	{
		AddError(TEXT("Design2UMG settings unavailable"));
		return false;
	}

	Design2UMGAutomationTestAdapter::AddExpectedErrorPlain(*this, TEXT("Too many unique remote images"), EAutomationExpectedErrorFlags::Contains, 2);
	const int32 UrlLimit = DesignImageResources::FImageResourceLimits::DefaultMaxUrlsPerOperation;
	FUMGWidgetData Data;
	Data.WidgetName = TEXT("WBP_ResourceFailure_UrlLimit");
	Data.RootWidget = MakeResourceFailureCanvas(TEXT("ResourceFailure_UrlLimitRoot"));
	for (int32 Index = 0; Index <= UrlLimit; ++Index)
	{
		Data.RootWidget.Children.Add(MakeResourceFailureImageNode(
			FString::Printf(TEXT("ResourceFailure_Url_%d"), Index),
			FString::Printf(TEXT("https://cdn.example/resource_failure_%d.png"), Index)));
	}

	FDesignImageImportResult ImageResult = DesignImportPlanning::ResolveImages(Data);
	TestTrue(TEXT("The URL-count limit ends the run"), ImageResult.HasFailures());
	if (TestEqual(TEXT("The limit records one diagnostic"), ImageResult.Diagnostics.Num(), 1))
	{
		TestEqual(TEXT("The limit has a typed budget category"),
			static_cast<uint8>(ImageResult.Diagnostics[0].Category),
			static_cast<uint8>(EDesignImageDiagnosticCategory::Budget));
		TestTrue(TEXT("The typed reason names the real limit"),
			ImageResult.Diagnostics[0].Reason.Contains(TEXT("Too many unique remote images")));
	}
	TestEqual(TEXT("The failure does not enter general warnings"), Data.Warnings.Num(), 0);

	Data.RootWidget.Children.Reset();
	Data.RootWidget.Children.Add(MakeResourceFailureImageNode(
		TEXT("ResourceFailure_Local"),
		TEXT("/Game/Design2UMG/Test/T_ResourceFailure.T_ResourceFailure")));
	ImageResult = DesignImportPlanning::ResolveImages(Data);
	TestFalse(TEXT("The retry clears the prior typed limit failure"), ImageResult.HasFailures());
	TestEqual(TEXT("General warnings remain clean"), Data.Warnings.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaMissingImagesObjectIsFailureTest,
	"Design2UMG.Resources.Failure.FigmaMissingImagesObjectIsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaMissingImagesObjectIsFailureTest::RunTest(const FString& Parameters)
{
	AddExpectedError(TEXT("No 'images' in meta"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });

	bool bCompleted = false;
	bool bSuccess = true;
	FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"), MakeDocumentWithImageRef(TEXT("ref-alpha")),
		[&bCompleted, &bSuccess](const bool bInSuccess)
		{
			bCompleted = true;
			bSuccess = bInSuccess;
		});

	TestEqual(TEXT("One image-refs request was started"), Transport.Pending.Num(), 1);
	if (Transport.Pending.Num() != 1)
	{
		return false;
	}

	// The document asked for image refs, but the response omits meta.images entirely.
	Transport.Complete(Transport.LastStartedId, TEXT("{\"meta\":{\"err\":null}}"), 200);

	TestTrue(TEXT("Completion callback fired"), bCompleted);
	TestFalse(TEXT("A response missing meta.images is reported as a failure"), bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaEmptyImagesDictionaryIsAllowedSubstituteTest,
	"Design2UMG.Resources.Failure.FigmaEmptyImagesDictionaryIsAllowedSubstitute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaEmptyImagesDictionaryIsAllowedSubstituteTest::RunTest(const FString& Parameters)
{
	// Plain string: the message contains "(s)", which would be read as a regex group
	// by AddExpectedError's default regex matching.
	Design2UMGAutomationTestAdapter::AddExpectedErrorPlain(*this, TEXT("requested image ref(s) not resolved"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });

	bool bCompleted = false;
	bool bSuccess = false;
	const auto Document = MakeDocumentWithVectorRasterFallbackRef(TEXT("ref-alpha"));
	FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"),
		Document,
		[&bCompleted, &bSuccess](const bool bInSuccess)
		{
			bCompleted = true;
			bSuccess = bInSuccess;
		});

	TestEqual(TEXT("One image-refs request was started"), Transport.Pending.Num(), 1);
	if (Transport.Pending.Num() != 1)
	{
		return false;
	}

	// meta.images exists but does not contain the requested ref. The ref is a
	// vector-raster fallback, which owns an explicit alternative rendering path, so
	// falling back to vector rendering is an allowed substitute, not a hard failure.
	Transport.Complete(Transport.LastStartedId, TEXT("{\"meta\":{\"images\":{}}}"), 200);

	TestTrue(TEXT("Completion callback fired"), bCompleted);
	TestTrue(TEXT("An empty images dictionary with an allowed substitute stays success"), bSuccess);
	TestTrue(TEXT("Missing fallback is removed so the builder uses geometry"),
		Document->Children[0]->Visual.VectorRasterFallbackImageRef.IsEmpty());
	TestFalse(TEXT("Substitution preserves vector geometry"), Document->Children[0]->Shape.Geometry.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaUnresolvedBitmapFillRefIsFailureTest,
	"Design2UMG.Resources.Failure.FigmaUnresolvedBitmapFillRefIsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaUnresolvedBitmapFillRefIsFailureTest::RunTest(const FString& Parameters)
{
	// Plain string: the message contains "(s)", which would be read as a regex group
	// by AddExpectedError's default regex matching.
	//
	// Only the required-ref message is expected here. The two missing-ref cases are
	// reported separately now, so a required ref is no longer also logged as "will fall
	// back to vector" - that message would contradict this test's whole point.
	Design2UMGAutomationTestAdapter::AddExpectedErrorPlain(*this, TEXT("required image ref(s) not resolved"), EAutomationExpectedErrorFlags::Contains, 0);

	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });

	bool bCompleted = false;
	bool bSuccess = true;
	FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"), MakeDocumentWithImageRef(TEXT("ref-alpha")),
		[&bCompleted, &bSuccess](const bool bInSuccess)
		{
			bCompleted = true;
			bSuccess = bInSuccess;
		});

	TestEqual(TEXT("One image-refs request was started"), Transport.Pending.Num(), 1);
	if (Transport.Pending.Num() != 1)
	{
		return false;
	}

	// A plain bitmap IMAGE fill has no alternative rendering path: if its ref is not in
	// the response the image is simply missing, so this must be reported as a real
	// failure instead of a silent "fell back to vector" success.
	Transport.Complete(Transport.LastStartedId, TEXT("{\"meta\":{\"images\":{}}}"), 200);

	TestTrue(TEXT("Completion callback fired"), bCompleted);
	TestFalse(TEXT("An unresolved plain bitmap image ref is a failure, not an allowed substitute"), bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaResolvedRefIsAppliedTest,
	"Design2UMG.Resources.Failure.FigmaResolvedRefIsApplied",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaResolvedRefIsAppliedTest::RunTest(const FString& Parameters)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });

	const TSharedRef<FDesignDocument> Document = MakeDocumentWithImageRef(TEXT("ref-alpha"));
	bool bCompleted = false;
	bool bSuccess = false;
	FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"), Document,
		[&bCompleted, &bSuccess](const bool bInSuccess)
		{
			bCompleted = true;
			bSuccess = bInSuccess;
		});

	TestEqual(TEXT("One image-refs request was started"), Transport.Pending.Num(), 1);
	if (Transport.Pending.Num() != 1)
	{
		return false;
	}

	Transport.Complete(Transport.LastStartedId,
		TEXT("{\"meta\":{\"images\":{\"ref-alpha\":\"https://cdn.example/resource_failure.png\"}}}"), 200);

	TestTrue(TEXT("Completion callback fired"), bCompleted);
	TestTrue(TEXT("A fully resolved image dictionary reports success"), bSuccess);
	TestTrue(TEXT("Resolved URL is applied to the requesting fill"),
		Document->Children.Num() == 1
			&& Document->Children[0]->Visual.Fills.Num() == 1
			&& Document->Children[0]->Visual.Fills[0].Image.Url == TEXT("https://cdn.example/resource_failure.png"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaNoImageRefsSkipsRequestTest,
	"Design2UMG.Resources.Failure.FigmaNoImageRefsSkipsRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaNoImageRefsSkipsRequestTest::RunTest(const FString& Parameters)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });

	const TSharedRef<FDesignDocument> Document = MakeShared<FDesignDocument>();
	Document->Children.Add(Design2UMGEditorTests::MakeNode(
		TEXT("resource_failure_plain"), TEXT("ResourceFailure_Plain"), EDesignNodeType::Rectangle, 64.0f, 64.0f));

	bool bCompleted = false;
	bool bSuccess = false;
	FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"), Document,
		[&bCompleted, &bSuccess](const bool bInSuccess)
		{
			bCompleted = true;
			bSuccess = bInSuccess;
		});

	TestEqual(TEXT("A document with no image refs starts no request"), Transport.Pending.Num(), 0);
	TestTrue(TEXT("Completion callback fired synchronously"), bCompleted);
	TestTrue(TEXT("Zero image refs is a normal success"), bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureUnproducedRequiredTextureIsFailureTest,
	"Design2UMG.Resources.Failure.UnproducedRequiredTextureIsFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureUnproducedRequiredTextureIsFailureTest::RunTest(const FString& Parameters)
{
	// A declared affine crop is a required resource: the plan is established, but the
	// source texture cannot be loaded, so the creator returns null without an error.
	// That is an identified failure, not a "no request" skip.
	const FString SourcePath = TEXT("/Game/Design2UMG/ResourceFailure_Missing/T_ResourceFailure_Absent.T_ResourceFailure_Absent");

	FUMGWidgetNode Root;
	Root.Type = UMGWidgetTypes::CanvasPanel;
	Root.Id = TEXT("ResourceFailure_Root");
	Root.Name = TEXT("ResourceFailure_Root");
	Root.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 128.0f, 128.0f };

	FUMGWidgetNode CropNode;
	CropNode.Type = UMGWidgetTypes::Image;
	CropNode.Id = TEXT("ResourceFailure_AffineCrop");
	CropNode.Name = TEXT("ResourceFailure_AffineCrop");
	CropNode.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 64.0f, 64.0f };
	CropNode.Brush.Image.TexturePath = SourcePath;
	CropNode.Brush.Image.Mode = TEXT("Crop");
	FUMGVisualBrushData::FImageTransformData Transform;
	Transform.A = 0.5f;
	Transform.D = 0.5f;
	Transform.B = 0.25f;
	Transform.C = -0.25f;
	CropNode.Brush.Image.Transform = Transform;
	Root.Children.Add(CropNode);

	FString Error;
	const bool bNormalized = FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
		Root, TEXT("/Game/Design2UMG/ResourceFailure"), TEXT("ResourceFailure"), &Error);

	TestFalse(TEXT("An unproduced required generated texture is reported as a failure"), bNormalized);
	TestTrue(TEXT("Failure names the missing generated texture"), Error.Contains(TEXT("was not produced")));
	TestTrue(TEXT("Failure names the offending node"), Error.Contains(TEXT("ResourceFailure_AffineCrop")));
	TestTrue(TEXT("The failed node's texture path is left untouched"),
		Root.Children.Num() == 1 && Root.Children[0].Brush.Image.TexturePath.Get(FString()) == SourcePath);

	// Control: a node with no crop request at all is a genuine "nothing to do" case and
	// must still succeed, so the failure above is not simply "always reject".
	FUMGWidgetNode PlainRoot;
	PlainRoot.Type = UMGWidgetTypes::CanvasPanel;
	PlainRoot.Id = TEXT("ResourceFailure_PlainRoot");
	PlainRoot.Name = TEXT("ResourceFailure_PlainRoot");
	PlainRoot.Bounds = FDesignPixelBounds{ 0.0f, 0.0f, 128.0f, 128.0f };
	FUMGWidgetNode PlainImage;
	PlainImage.Type = UMGWidgetTypes::Image;
	PlainImage.Id = TEXT("ResourceFailure_PlainImage");
	PlainImage.Name = TEXT("ResourceFailure_PlainImage");
	PlainRoot.Children.Add(PlainImage);

	FString PlainError;
	TestTrue(TEXT("A tree with no image-transform request still succeeds"),
		FPreBuildResourceNormalizer::NormalizeImageTransformTextures(
			PlainRoot, TEXT("/Game/Design2UMG/ResourceFailure"), TEXT("ResourceFailure"), &PlainError));
	TestTrue(TEXT("No request produces no error"), PlainError.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureOptionalEmptyRasterAndTextureFallbackTest,
	"Design2UMG.Resources.Failure.OptionalEmptyRasterAndTextureFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureOptionalEmptyRasterAndTextureFallbackTest::RunTest(const FString& Parameters)
{
	const FString Folder = TEXT("/Game/Design2UMG/ResourceFailure_Optional_")
		+ FGuid::NewGuid().ToString(EGuidFormats::Digits);
	ON_SCOPE_EXIT { Design2UMGEditorTests::DeleteGeneratedAssetsAtPath(Folder); };

	FUMGWidgetNode TransparentPath;
	TransparentPath.Type = UMGWidgetTypes::Image;
	TransparentPath.Id = TEXT("transparent_path");
	TransparentPath.Name = TEXT("TransparentPath");
	TransparentPath.Path.Geometry = TEXT("M0 0 L16 0 L16 16 L0 16 Z");
	TransparentPath.Path.FillColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.0f);
	TransparentPath.WidgetStyle.WidthOverride = 16.0f;
	TransparentPath.WidgetStyle.HeightOverride = 16.0f;

	FString Error;
	TestTrue(TEXT("A path with no visible fill or stroke needs no generated texture"),
		FPreBuildResourceNormalizer::NormalizePathTextures(
			TransparentPath, Folder / TEXT("Transparent"), TEXT("ResourceFailure"), &Error));
	TestTrue(TEXT("The legal no-op leaves no failure text"), Error.IsEmpty());
	TestTrue(TEXT("The legal no-op does not invent a texture path"),
		TransparentPath.Brush.Image.TexturePath.Get(FString()).IsEmpty());

	FUMGWidgetNode OpaquePath = TransparentPath;
	OpaquePath.Id = TEXT("opaque_path");
	OpaquePath.Name = TEXT("OpaquePath");
	OpaquePath.Path.FillColor = FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
	TestTrue(TEXT("The same path with visible paint still materializes"),
		FPreBuildResourceNormalizer::NormalizePathTextures(
			OpaquePath, Folder / TEXT("Opaque"), TEXT("ResourceFailure"), &Error));
	TestFalse(TEXT("Visible paint receives a persisted texture path"),
		OpaquePath.Brush.Image.TexturePath.Get(FString()).IsEmpty());

	FUMGWidgetNode Fallback;
	Fallback.Type = UMGWidgetTypes::Image;
	Fallback.Id = TEXT("unknown_material_with_texture");
	Fallback.Name = TEXT("UnknownMaterialWithTexture");
	Fallback.Brush.MaterialRef = TEXT("ExternalMaterialNotAProceduralShape");
	Fallback.Brush.Image.TexturePath = TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture");
	Fallback.WidgetStyle.WidthOverride = 16.0f;
	Fallback.WidgetStyle.HeightOverride = 16.0f;
	const FString OriginalTexturePath = Fallback.Brush.Image.TexturePath.GetValue();
	TestTrue(TEXT("An unknown procedural ref keeps its valid texture substitute"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Fallback, Folder / TEXT("Fallback"), TEXT("ResourceFailure"), &Error));
	TestTrue(TEXT("The texture substitute is preserved exactly"),
		Fallback.Brush.Image.TexturePath.IsSet()
		&& Fallback.Brush.Image.TexturePath.GetValue() == OriginalTexturePath);
	TestTrue(TEXT("The substitute path is not reported as a generation failure"), Error.IsEmpty());
	FUMGWidgetNode Missing = Fallback;
	Missing.Id = TEXT("unknown_material_without_texture");
	Missing.Name = TEXT("UnknownMaterialWithoutTexture");
	Missing.Brush.Image.TexturePath.Reset();
	AddExpectedError(
		TEXT("Required generated texture for node 'UnknownMaterialWithoutTexture'"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("An unknown procedural ref without a substitute is a required-resource failure"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			Missing, Folder / TEXT("Missing"), TEXT("ResourceFailure"), &Error));
	TestTrue(TEXT("The missing-resource error identifies the node and absent output"),
		Error.Contains(TEXT("UnknownMaterialWithoutTexture"))
		&& Error.Contains(TEXT("was not produced")));

	FUMGWidgetNode BrokenSubstitute = Fallback;
	BrokenSubstitute.Id = TEXT("unknown_material_with_invalid_texture");
	BrokenSubstitute.Name = TEXT("UnknownMaterialWithInvalidTexture");
	BrokenSubstitute.Brush.Image.TexturePath =
		TEXT("/Game/Design2UMG/ResourceFailure_Missing/T_NotThere.T_NotThere");
	AddExpectedError(
		TEXT("Required generated texture for node 'UnknownMaterialWithInvalidTexture'"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestFalse(TEXT("A non-loadable texture string is not a valid material substitute"),
		FPreBuildResourceNormalizer::NormalizeProceduralTextures(
			BrokenSubstitute, Folder / TEXT("BrokenSubstitute"), TEXT("ResourceFailure"), &Error));
	TestTrue(TEXT("The invalid-substitute error identifies the node and absent output"),
		Error.Contains(TEXT("UnknownMaterialWithInvalidTexture"))
		&& Error.Contains(TEXT("was not produced")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FResourceFailureFigmaSubstituteMustCoverEveryUseTest,
	"Design2UMG.Resources.Failure.FigmaSubstituteMustCoverEveryUse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FResourceFailureFigmaSubstituteMustCoverEveryUseTest::RunTest(const FString& Parameters)
{
	Design2UMGAutomationTestAdapter::AddExpectedErrorPlain(*this, TEXT("required image ref(s) not resolved"), EAutomationExpectedErrorFlags::Contains, 0);
	for (int32 CaseIndex = 0; CaseIndex < 3; ++CaseIndex)
	{
		const auto Document = MakeDocumentWithVectorRasterFallbackRef(TEXT("shared-ref"));
		if (CaseIndex == 0)
		{
			Document->Children.Append(MakeDocumentWithImageRef(TEXT("shared-ref"))->Children);
		}
		else if (CaseIndex == 1)
		{
			Document->Children[0]->Shape.Geometry.Reset();
		}
		else
		{
			Document->Children[0]->Visual.Fills.Reset();
			Document->Children[0]->Visual.Stroke.Thickness.Uniform = 1.0f;
			Document->Children[0]->Visual.Stroke.Fill.Reset();
		}
		FScopedHttpTestTransport Transport;
		FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });
		bool bCompleted = false;
		bool bSucceeded = true;
		FFigmaImageExporter::ResolveImageRefs(Client, TEXT("resource_failure-file"), Document,
			[&](bool bResult) { bCompleted = true; bSucceeded = bResult; });
		if (!TestEqual(TEXT("One request is made"), Transport.Pending.Num(), 1)) return false;
		Transport.Complete(Transport.LastStartedId, TEXT("{\"meta\":{\"images\":{}}}"), 200);
		TestTrue(TEXT("Missing-reference resolution completes"), bCompleted);
		TestFalse(FString::Printf(TEXT("Missing ref fails when its substitute is incomplete (case %d)"), CaseIndex), bSucceeded);
	}
	return true;
}

#endif // WITH_AUTOMATION_WORKER
