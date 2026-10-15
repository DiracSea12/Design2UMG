// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"
#include "DesignImportGeneration.h"
#include "Design2UMGNotifications.h"
#include "PencilConnector.h"
#include "Tests/Design2UMGMCPStdioTestProcess.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Misc/AutomationTest.h"
#include "Misc/FeedbackContext.h"
#include "Misc/ScopeExit.h"
#include "UMGGeneratorRootHandoff.h"
#include "WidgetBlueprint.h"
#include "UObject/Package.h"

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

namespace
{
	FUMGWidgetData MakeCancelBoundaryWidgetData(const FString& WidgetName)
	{
		FUMGWidgetData Data;
		Data.WidgetName = WidgetName;
		Data.RootWidget.Type = TEXT("CanvasPanel");
		Data.RootWidget.Id = TEXT("cancel_root");
		Data.RootWidget.Name = TEXT("CancelRoot");
		Data.RootWidget.Bounds.W = 96.0f;
		Data.RootWidget.Bounds.H = 64.0f;
		return Data;
	}

	class FCancelAfterProgressPollFeedbackContext final : public FFeedbackContext
	{
	public:
		explicit FCancelAfterProgressPollFeedbackContext(const int32 InCancelOnPoll)
			: CancelOnPoll(InCancelOnPoll)
		{
		}

		virtual bool ReceivedUserCancel() override
		{
			return ++PollCount >= CancelOnPoll;
		}

		virtual void Serialize(const TCHAR*, ELogVerbosity::Type, const FName&) override
		{
		}

	private:
		int32 CancelOnPoll = 0;
		int32 PollCount = 0;
	};

	struct FScopedNotificationCapture
	{
		TArray<FString> Warnings;
		TArray<FString> Successes;
		FDesign2UMGMessageNotificationHandler PreviousWarning;
		FDesign2UMGSuccessNotificationHandler PreviousSuccess;

		FScopedNotificationCapture()
		{
			PreviousWarning = FDesign2UMGNotification::RegisterWarningHandler(
				FDesign2UMGMessageNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnWarning));
			PreviousSuccess = FDesign2UMGNotification::RegisterSuccessHandler(
				FDesign2UMGSuccessNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnSuccess));
		}

		~FScopedNotificationCapture()
		{
			FDesign2UMGNotification::RegisterWarningHandler(MoveTemp(PreviousWarning));
			FDesign2UMGNotification::RegisterSuccessHandler(MoveTemp(PreviousSuccess));
		}

		void OnWarning(const FText& Message)
		{
			Warnings.Add(Message.ToString());
		}

		void OnSuccess(const FText& Message, const FString&)
		{
			Successes.Add(Message.ToString());
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNotificationCaptureRestoresHandlersTest,
	"Design2UMG.Notifications.CaptureRestoresHandlers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNotificationCaptureRestoresHandlersTest::RunTest(const FString&)
{
	int32 Warnings = 0, Successes = 0, Errors = 0;
	auto PreviousWarning = FDesign2UMGNotification::RegisterWarningHandler(
		FDesign2UMGMessageNotificationHandler::CreateLambda([&](const FText&) { ++Warnings; }));
	auto PreviousSuccess = FDesign2UMGNotification::RegisterSuccessHandler(
		FDesign2UMGSuccessNotificationHandler::CreateLambda([&](const FText&, const FString&) { ++Successes; }));
	auto PreviousError = FDesign2UMGNotification::RegisterErrorHandler(
		FDesign2UMGMessageNotificationHandler::CreateLambda([&](const FText&) { ++Errors; }));
	ON_SCOPE_EXIT
	{
		FDesign2UMGNotification::RegisterWarningHandler(MoveTemp(PreviousWarning));
		FDesign2UMGNotification::RegisterSuccessHandler(MoveTemp(PreviousSuccess));
		FDesign2UMGNotification::RegisterErrorHandler(MoveTemp(PreviousError));
	};
	const FText Message = FText::FromString(TEXT("Scoped notification"));
	{
		FScopedNotificationCapture Outer;
		{
			FScopedNotificationCapture Inner;
			FDesign2UMGNotification::Warning(Message);
			TestEqual(TEXT("Inner scope receives the warning"), Inner.Warnings.Num(), 1);
			TestEqual(TEXT("Outer scope is temporarily replaced"), Outer.Warnings.Num(), 0);
		}
		FDesign2UMGNotification::Warning(Message);
		FDesign2UMGNotification::Success(Message);
		FDesign2UMGNotification::Error(Message);
		TestEqual(TEXT("Outer warning handler is restored"), Outer.Warnings.Num(), 1);
		TestEqual(TEXT("Outer success handler is restored"), Outer.Successes.Num(), 1);
	}
	FDesign2UMGNotification::Warning(Message);
	FDesign2UMGNotification::Success(Message);
	FDesign2UMGNotification::Error(Message);
	TestEqual(TEXT("Original warning handler is restored"), Warnings, 1);
	TestEqual(TEXT("Original success handler is restored"), Successes, 1);
	TestEqual(TEXT("Unrelated error handler survives both scopes"), Errors, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FUMGGeneratorRootPreparationTest,
	"Design2UMG.Generation.RootPreparation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FUMGGeneratorRootPreparationTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("root");
	WidgetData.RootWidget.Name = TEXT("Root");
	WidgetData.RootWidget.Bounds.W = 320.0f;
	WidgetData.RootWidget.Bounds.H = 180.0f;

	const UMGGeneratorRootHandoff::FPreparedRoot Prepared =
		UMGGeneratorRootHandoff::PrepareRootForBuild(WidgetData, TEXT("/Game/D2U_Test"), TEXT("WBP_D2U_Test"));

	TestEqual(TEXT("Root handoff keeps the root widget type"), Prepared.Root.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Root handoff derives design size from valid root bounds"), Prepared.DesignSize, FIntPoint(320, 180));
	TestEqual(TEXT("Root handoff preserves root id"), Prepared.Root.Id, FString(TEXT("root")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FImportPlanningTest,
	"Design2UMG.Import.Planning",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FImportPlanningTest::RunTest(const FString& Parameters)
{
	FUMGWidgetData WidgetData;
	WidgetData.WidgetName = TEXT("WBP_ImportPlanningSmoke");
	WidgetData.RootWidget.Type = TEXT("CanvasPanel");
	WidgetData.RootWidget.Id = TEXT("planning_root");
	WidgetData.RootWidget.Name = TEXT("PlanningRoot");
	WidgetData.RootWidget.Bounds.W = 256.0f;
	WidgetData.RootWidget.Bounds.H = 128.0f;

	FImportAnalysisInput Input;
	Input.bResolveImages = false;
	const FImportInPlaceAnalysisResult Result = DesignImportPlanning::AnalyzeWidgetData(WidgetData, Input);

	TestEqual(TEXT("Import planning keeps widget name in place"), WidgetData.WidgetName, FString(TEXT("WBP_ImportPlanningSmoke")));
	TestEqual(TEXT("Import planning keeps root type in place"), WidgetData.RootWidget.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Import planning keeps root bounds width in place"), WidgetData.RootWidget.Bounds.W, 256.0f);
	TestTrue(TEXT("In-place analysis succeeds"), Result.ErrorMessage.IsEmpty());
	TestFalse(TEXT("Import planning without image resolution produces no image diagnostics"), Result.ImageResult.HasDiagnostics());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPencilConnectionLifecycleTest,
 "Design2UMG.Pencil.ConnectionLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FPencilConnectionLifecycleTest::RunTest(const FString&)
{
 FPencilConnector Connector;
 if (!Design2UMGEditorTests::ConnectStdioFixture(*this, Connector.GetClient())) return false;
 TestTrue(TEXT("Connector reports its connected child"), Connector.IsRunning());
 int32 Callbacks = 0;
 Connector.GetClient().CallToolAsync(TEXT("slow"), nullptr, [&](bool, const FString&) { ++Callbacks; });
 Connector.Shutdown();
 TestFalse(TEXT("Shutdown closes the server and its polling connection"), Connector.IsRunning());
 TestFalse(TEXT("Shutdown clears client connection"), Connector.GetClient().IsConnected());
 TestEqual(TEXT("Shutdown silently cancels pending import work"), Callbacks, 0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGenerationCancelBoundaryBehaviorTest,
	"Design2UMG.Generation.Cancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FGenerationCancelBoundaryBehaviorTest::RunTest(const FString& Parameters)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/GenerationCancelBoundary");
	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FCancelAfterProgressPollFeedbackContext Cancellation(2);
		UWidgetBlueprint* CancelledBlueprint = DesignImportGeneration::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_GenerationCancelBeforeMutation")),
			OutputDir,
			nullptr,
			nullptr,
			&Cancellation);

		TestNull(TEXT("Pre-mutation cancel returns null"), CancelledBlueprint);
		TestEqual(TEXT("Pre-mutation cancel emits one warning"), Notifications.Warnings.Num(), 1);
		TestEqual(TEXT("Pre-mutation cancel emits no success"), Notifications.Successes.Num(), 0);
		TestNull(TEXT("Pre-mutation cancel does not create Blueprint asset"),
			FindObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Design2UMG/_TestOutput/GenerationCancelBoundary/WBP_GenerationCancelBeforeMutation.WBP_GenerationCancelBeforeMutation")));
	}

	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FCancelAfterProgressPollFeedbackContext Cancellation(6);
		UWidgetBlueprint* CompletedBlueprint = DesignImportGeneration::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_GenerationCancelAfterMutation")),
			OutputDir,
			nullptr,
			nullptr,
			&Cancellation);

		TestNotNull(TEXT("Post-mutation cancel completes generation"), CompletedBlueprint);
		if (CompletedBlueprint)
		{
			TestTrue(TEXT("Post-mutation cancel leaves a coherent saved Blueprint package"),
				!CompletedBlueprint->GetOutermost()->IsDirty());
			TestTrue(TEXT("Post-mutation cancel generated a valid WidgetTree"), CompletedBlueprint->WidgetTree != nullptr);
		}
		TestEqual(TEXT("Post-mutation cancel emits one warning"), Notifications.Warnings.Num(), 1);
		TestEqual(TEXT("Post-mutation cancel emits no normal success summary"), Notifications.Successes.Num(), 0);
	}

	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		UWidgetBlueprint* CompletedBlueprint = DesignImportGeneration::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_UncancelledGeneration")),
			OutputDir);

		TestNotNull(TEXT("Uncancelled generation completes"), CompletedBlueprint);
		TestEqual(TEXT("Uncancelled generation emits no warning"), Notifications.Warnings.Num(), 0);
		TestEqual(TEXT("Uncancelled generation emits normal success summary"), Notifications.Successes.Num(), 1);
	}

	DeleteGeneratedAssetsAtPath(OutputDir);
	return true;
}
