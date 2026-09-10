// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignImportPlanning.h"
#include "DesignImportUtils.h"
#include "Design2UMGNotifications.h"
#include "ExternalServices/DesignHttpClientSlot.h"
#include "FigmaHttpClient.h"
#include "MCPHttpClient.h"
#include "PencilConnector.h"
#include "Tests/Design2UMGTestAssetCleanup.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "UMGGeneratorRootHandoff.h"
#include "WidgetBlueprint.h"

using Design2UMGEditorTests::DeleteGeneratedAssetsAtPath;

struct FPencilConnectorTestAccess
{
	static void ScheduleRetry(FPencilConnector& Connector)
	{
		Connector.ScheduleRetry(FTickerDelegate::CreateLambda([](float) { return true; }), 60.0f);
	}

	static bool HasRetryTicker(const FPencilConnector& Connector)
	{
		return Connector.RetryTickerHandle.IsValid();
	}
};

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

	struct FScopedGenerationCancelPoint
	{
		explicit FScopedGenerationCancelPoint(const DesignImportUtils::EAutomationGenerationCancelPoint CancelPoint)
		{
			DesignImportUtils::SetAutomationGenerationCancelPoint(CancelPoint);
		}

		~FScopedGenerationCancelPoint()
		{
			DesignImportUtils::ResetAutomationGenerationCancelPoint();
		}
	};

	struct FScopedNotificationCapture
	{
		TArray<FString> Warnings;
		TArray<FString> Successes;

		FScopedNotificationCapture()
		{
			FDesign2UMGNotification::RegisterWarningHandler(
				FDesign2UMGMessageNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnWarning));
			FDesign2UMGNotification::RegisterSuccessHandler(
				FDesign2UMGSuccessNotificationHandler::CreateRaw(this, &FScopedNotificationCapture::OnSuccess));
		}

		~FScopedNotificationCapture()
		{
			FDesign2UMGNotification::ResetHandlers();
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDUMGGeneratorRootHandoffSmokeTest,
	"Design2UMG.BatchD.UMGGeneratorRootHandoffSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDUMGGeneratorRootHandoffSmokeTest::RunTest(const FString& Parameters)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDImportPlanningSmokeTest,
	"Design2UMG.BatchD.ImportPlanningSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDImportPlanningSmokeTest::RunTest(const FString& Parameters)
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
	const FImportAnalysisResult Result = DesignImportPlanning::AnalyzeWidgetData(WidgetData, Input);

	TestEqual(TEXT("Import planning copies widget name"), Result.Data.WidgetName, WidgetData.WidgetName);
	TestEqual(TEXT("Import planning copies root type"), Result.Data.RootWidget.Type, FString(TEXT("CanvasPanel")));
	TestEqual(TEXT("Import planning copies root bounds width"), Result.Data.RootWidget.Bounds.W, 256.0f);
	TestTrue(TEXT("Import planning without image resolution produces no image report"), Result.ImageReport.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDPencilRetryTickerLifecycleTest,
	"Design2UMG.BatchD.PencilRetryTickerLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDPencilRetryTickerLifecycleTest::RunTest(const FString& Parameters)
{
	FPencilConnector Connector;
	FPencilConnectorTestAccess::ScheduleRetry(Connector);
	TestTrue(TEXT("Pencil connector reports a scheduled retry ticker"), FPencilConnectorTestAccess::HasRetryTicker(Connector));
	Connector.Shutdown();
	TestFalse(TEXT("Shutdown unregisters the retry ticker"), FPencilConnectorTestAccess::HasRetryTicker(Connector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDFigmaHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.FigmaHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDFigmaHttpCancellationBehaviorTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		int32 StartCount = 0;
		int32 CancelCount = 0;
		FDesignHttpClientSlotSimulatedCompletion Completion;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FDesignHttpClientSlotAutomationHooks> Hooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	Hooks->StartRequest =
		[HookState](int32, FDesignHttpClientSlotSimulatedCompletion OnComplete)
		{
			++HookState->StartCount;
			HookState->Completion = MoveTemp(OnComplete);
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32)
		{
			++HookState->CancelCount;
		};

	SetDesignHttpClientSlotAutomationHooks(Hooks);

	bool bCallbackCalled = false;
	{
		FFigmaHttpClient Client;
		Client.SetToken(TEXT("phase24-token"));
		Client.GetFile(TEXT("phase24-file"), [&bCallbackCalled](bool, const FString&)
		{
			bCallbackCalled = true;
		});

		TestEqual(TEXT("Figma behavior hook started one request"), HookState->StartCount, 1);
		Client.CancelRequests();
		TestEqual(TEXT("Figma behavior hook observed one cancellation"), HookState->CancelCount, 1);
		TestTrue(TEXT("Figma behavior hook captured completion"), static_cast<bool>(HookState->Completion));

		HookState->Completion(false, TEXT(""), 0, TEXT(""));
		TestFalse(TEXT("Cancelled Figma request suppresses late callback"), bCallbackCalled);
	}

	ResetDesignHttpClientSlotAutomationHooks();

	TSharedRef<FDesignHttpClientSlotAutomationHooks> FailHooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	FailHooks->StartRequest =
		[](int32, FDesignHttpClientSlotSimulatedCompletion)
		{
			return false;
		};
	SetDesignHttpClientSlotAutomationHooks(FailHooks);

	bool bFailureCalled = false;
	bool bFailureSuccess = true;
	{
		FFigmaHttpClient Client;
		Client.SetToken(TEXT("phase24-token"));
		Client.GetFile(TEXT("phase24-file"), [&bFailureCalled, &bFailureSuccess](bool bSuccess, const FString&)
		{
			bFailureCalled = true;
			bFailureSuccess = bSuccess;
		});
	}

	ResetDesignHttpClientSlotAutomationHooks();

	TestTrue(TEXT("Synchronous Figma launch failure reports callback"), bFailureCalled);
	TestFalse(TEXT("Synchronous Figma launch failure reports failure"), bFailureSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.MCPHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpCancellationBehaviorTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		TMap<int32, FDesignHttpClientSlotSimulatedCompletion> PendingByTrackingId;
		int32 LastStartedTrackingId = 0;
		int32 CancelCount = 0;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FDesignHttpClientSlotAutomationHooks> Hooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	Hooks->StartRequest =
		[HookState](int32 TrackingId, FDesignHttpClientSlotSimulatedCompletion OnComplete)
		{
			HookState->PendingByTrackingId.Add(TrackingId, MoveTemp(OnComplete));
			HookState->LastStartedTrackingId = TrackingId;
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32)
		{
			++HookState->CancelCount;
		};

	SetDesignHttpClientSlotAutomationHooks(Hooks);

	FMCPHttpClient Client;
	bool bConnectCalled = false;
	bool bToolCallbackCalled = false;
	Client.ConnectAsync(TEXT("http://phase24.test"), [&bConnectCalled](bool bSuccess)
	{
		bConnectCalled = bSuccess;
	});

	const int32 InitializeTrackingId = HookState->LastStartedTrackingId;
	TestTrue(TEXT("MCP behavior hook captured initialize"), HookState->PendingByTrackingId.Contains(InitializeTrackingId));
	HookState->PendingByTrackingId[InitializeTrackingId](true, TEXT("{}"), 200, TEXT("phase24-session"));
	TestTrue(TEXT("MCP handshake chained the initialized notification"), HookState->LastStartedTrackingId != InitializeTrackingId);
	HookState->PendingByTrackingId[HookState->LastStartedTrackingId](true, TEXT(""), 202, TEXT(""));
	TestTrue(TEXT("Async MCP connect completes"), bConnectCalled);

	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(), [&bToolCallbackCalled](bool, const FString&)
	{
		bToolCallbackCalled = true;
	});

	TestTrue(TEXT("MCP behavior hook captured tools/call"), HookState->PendingByTrackingId.Contains(HookState->LastStartedTrackingId));
	const int32 ToolCallTrackingId = HookState->LastStartedTrackingId;
	Client.CancelRequests();
	TestEqual(TEXT("MCP behavior hook observed one cancellation"), HookState->CancelCount, 1);

	const FString ToolResponse =
		TEXT("event: message\n")
		TEXT("data: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"phase24\"}]}}\n\n");
	HookState->PendingByTrackingId[ToolCallTrackingId](true, ToolResponse, 200, TEXT(""));
	TestFalse(TEXT("Cancelled MCP async request suppresses late callback"), bToolCallbackCalled);

	ResetDesignHttpClientSlotAutomationHooks();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpClientSessionCycleReleaseTest,
	"Design2UMG.BatchD.MCPHttpClientSessionCycleRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpClientSessionCycleReleaseTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		TMap<int32, FDesignHttpClientSlotSimulatedCompletion> PendingByTrackingId;
		int32 LastStartedTrackingId = 0;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FDesignHttpClientSlotAutomationHooks> Hooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	Hooks->StartRequest =
		[HookState](int32 TrackingId, FDesignHttpClientSlotSimulatedCompletion OnComplete)
		{
			HookState->PendingByTrackingId.Add(TrackingId, MoveTemp(OnComplete));
			HookState->LastStartedTrackingId = TrackingId;
			return true;
		};
	Hooks->CancelRequest = [](int32) {};
	SetDesignHttpClientSlotAutomationHooks(Hooks);

	FMCPHttpClient Client;
	bool bConnected = false;
	Client.ConnectAsync(TEXT("http://phase24.test"), [&bConnected](bool bSuccess)
	{
		bConnected = bSuccess;
	});
	const auto WeakSession = Client.GetSessionWeakForTests();
	if (!TestTrue(TEXT("Session exists after ConnectAsync"), WeakSession.IsValid()))
	{
		ResetDesignHttpClientSlotAutomationHooks();
		return false;
	}

	// Complete both handshake hops: initialize -> notifications/initialized. The notification
	// completion leaves a delivered callback stored in the session; that stored copy is exactly
	// the self-reference the release fix must clear before Disconnect drops the member reference.
	int32 CompletedHandshakeSteps = 0;
	for (int32 Step = 0; Step < 2 && !bConnected; ++Step)
	{
		TArray<FDesignHttpClientSlotSimulatedCompletion> Callbacks;
		for (const TPair<int32, FDesignHttpClientSlotSimulatedCompletion>& Pair : HookState->PendingByTrackingId)
		{
			Callbacks.Add(Pair.Value);
		}
		HookState->PendingByTrackingId.Reset();
		if (Callbacks.IsEmpty())
		{
			break;
		}
		for (const FDesignHttpClientSlotSimulatedCompletion& Callback : Callbacks)
		{
			Callback(true, TEXT("{}"), 200, TEXT("phase24-session"));
			++CompletedHandshakeSteps;
		}
	}
	TestTrue(TEXT("MCP handshake completes"), bConnected);
	TestEqual(TEXT("Both handshake steps completed"), CompletedHandshakeSteps, 2);

	// Drop every external callback holder, then disconnect: a self-referencing session would
	// survive the member reset and keep the weak handle valid.
	HookState->PendingByTrackingId.Reset();
	ResetDesignHttpClientSlotAutomationHooks();
	TestTrue(TEXT("Client still owns the connected session before Disconnect"), WeakSession.IsValid());
	Client.Disconnect();
	TestFalse(TEXT("Disconnect releases the connected session"), WeakSession.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDGenerationCancelBoundaryBehaviorTest,
	"Design2UMG.BatchD.GenerationCancelBoundaryBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDGenerationCancelBoundaryBehaviorTest::RunTest(const FString& Parameters)
{
	const FString OutputDir = TEXT("/Game/Design2UMG/_TestOutput/Phase23CancelBoundary");
	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FScopedGenerationCancelPoint CancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::BeforeBuild);
		UWidgetBlueprint* CancelledBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelBeforeMutation")),
			OutputDir);

		TestNull(TEXT("Pre-mutation cancel returns null"), CancelledBlueprint);
		TestEqual(TEXT("Pre-mutation cancel emits one warning"), Notifications.Warnings.Num(), 1);
		TestEqual(TEXT("Pre-mutation cancel emits no success"), Notifications.Successes.Num(), 0);
		TestNull(TEXT("Pre-mutation cancel does not create Blueprint asset"),
			FindObject<UWidgetBlueprint>(nullptr, TEXT("/Game/Design2UMG/_TestOutput/Phase23CancelBoundary/WBP_Phase23CancelBeforeMutation.WBP_Phase23CancelBeforeMutation")));
	}

	DeleteGeneratedAssetsAtPath(OutputDir);

	{
		FScopedNotificationCapture Notifications;
		FScopedGenerationCancelPoint CancelPoint(DesignImportUtils::EAutomationGenerationCancelPoint::AfterBuildBeforeSave);
		UWidgetBlueprint* CompletedBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelAfterMutation")),
			OutputDir);

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
		UWidgetBlueprint* CompletedBlueprint = DesignImportUtils::GenerateBlueprint(
			MakeCancelBoundaryWidgetData(TEXT("WBP_Phase23CancelNoSeam")),
			OutputDir);

		TestNotNull(TEXT("No cancel seam keeps generation path working"), CompletedBlueprint);
		TestEqual(TEXT("No cancel seam emits no warning"), Notifications.Warnings.Num(), 0);
		TestEqual(TEXT("No cancel seam emits normal success summary"), Notifications.Successes.Num(), 1);
	}

	DeleteGeneratedAssetsAtPath(OutputDir);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPRequestIdSemanticsTest,
	"Design2UMG.BatchD.MCPRequestIdSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPRequestIdSemanticsTest::RunTest(const FString& Parameters)
{
	struct FHookState
	{
		TMap<int32, FDesignHttpClientSlotSimulatedCompletion> PendingByTrackingId;
		TArray<int32> CancelledIds;
		int32 LastStartedTrackingId = 0;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FDesignHttpClientSlotAutomationHooks> Hooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	Hooks->StartRequest =
		[HookState](int32 TrackingId, FDesignHttpClientSlotSimulatedCompletion OnComplete)
		{
			HookState->PendingByTrackingId.Add(TrackingId, MoveTemp(OnComplete));
			HookState->LastStartedTrackingId = TrackingId;
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32 TrackingId)
		{
			HookState->CancelledIds.Add(TrackingId);
		};

	SetDesignHttpClientSlotAutomationHooks(Hooks);

	FMCPHttpClient Client;
	Client.ConnectAsync(TEXT("http://phase24.test"), [](bool){});
	// Complete the FULL handshake: initialize chains notifications/initialized,
	// so drain twice with the snapshot-then-clear pattern (live iteration is
	// unsafe because completing one step queues the next into the same map).
	for (int32 HandshakeStep = 0; HandshakeStep < 2; ++HandshakeStep)
	{
		TArray<FDesignHttpClientSlotSimulatedCompletion> HandshakeCallbacks;
		for (const TPair<int32, FDesignHttpClientSlotSimulatedCompletion>& Pair
			: HookState->PendingByTrackingId)
		{
			HandshakeCallbacks.Add(Pair.Value);
		}
		HookState->PendingByTrackingId.Reset();
		if (HandshakeCallbacks.IsEmpty())
		{
			break;
		}
		for (const FDesignHttpClientSlotSimulatedCompletion& Callback : HandshakeCallbacks)
		{
			Callback(true, TEXT("{}"), 200, TEXT("phase24-session"));
		}
	}
	TestTrue(TEXT("Handshake fully drained before tool calls"),
		HookState->PendingByTrackingId.Num() == 0);

	// Start request A, then replace it with request B before A completes.
	int32 ACallbackCount = 0;
	bool bALastSuccess = false;
	bool bBCallbackFired = false;
	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(),
		[&ACallbackCount, &bALastSuccess](bool bSuccess, const FString&)
		{
			++ACallbackCount;
			bALastSuccess = bSuccess;
		});
	const int32 RequestATrackingId = HookState->LastStartedTrackingId;
	Client.CallToolAsync(TEXT("get_variables"), MakeShared<FJsonObject>(), [&bBCallbackFired](bool, const FString&)
	{
		bBCallbackFired = true;
	});
	const int32 RequestBTrackingId = HookState->LastStartedTrackingId;

	TestTrue(TEXT("Replacing an in-flight request cancels the old one"),
		HookState->CancelledIds.Contains(RequestATrackingId));
	TestTrue(TEXT("Replacement uses a fresh tracking id"), RequestBTrackingId != RequestATrackingId);

	// Superseding the in-flight request owes it exactly one failure delivery...
	TestEqual(TEXT("Superseded request receives exactly one callback"), ACallbackCount, 1);
	TestFalse(TEXT("Superseded request callback reports failure"), bALastSuccess);

	// ...while a late stale completion for the cancelled request must not
	// fire it again.
	HookState->PendingByTrackingId[RequestATrackingId](true,
		TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"stale\"}]}}\n\n"),
		200, TEXT(""));
	TestEqual(TEXT("Late stale completion does not re-fire the superseded callback"), ACallbackCount, 1);

	// The live replacement still delivers.
	HookState->PendingByTrackingId[RequestBTrackingId](true,
		TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"live\"}]}}\n\n"),
		200, TEXT(""));
	TestTrue(TEXT("Live replacement callback fires after cancel+replace"), bBCallbackFired);

	ResetDesignHttpClientSlotAutomationHooks();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPDisconnectCollisionTest,
	"Design2UMG.BatchD.MCPDisconnectCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPDisconnectCollisionTest::RunTest(const FString& Parameters)
{
	// Models the real cross-session race: a session-1 request is cancelled, the
	// client disconnects and reconnects, and THEN the late HTTP callback for the
	// old request arrives. Tracking ids stay monotonic, so the late callback
	// keeps its cancellation mark (no id reuse) and must be swallowed without
	// disturbing the new session's active tracking state.
	struct FHookState
	{
		TMap<int32, FDesignHttpClientSlotSimulatedCompletion> PendingByTrackingId;
		TArray<int32> CancelledIds;
		int32 LastStartedTrackingId = 0;
	};

	TSharedRef<FHookState> HookState = MakeShared<FHookState>();
	TSharedRef<FDesignHttpClientSlotAutomationHooks> Hooks = MakeShared<FDesignHttpClientSlotAutomationHooks>();
	Hooks->StartRequest =
		[HookState](int32 TrackingId, FDesignHttpClientSlotSimulatedCompletion OnComplete)
		{
			HookState->PendingByTrackingId.Add(TrackingId, MoveTemp(OnComplete));
			HookState->LastStartedTrackingId = TrackingId;
			return true;
		};
	Hooks->CancelRequest =
		[HookState](int32 TrackingId)
		{
			HookState->CancelledIds.Add(TrackingId);
		};

	SetDesignHttpClientSlotAutomationHooks(Hooks);

	FMCPHttpClient Client;

	// Snapshot-drain pending callbacks safely (completing a step can queue
	// another entry into the same map).
	auto DrainPending = [&HookState](FAutomationTestBase& Test, const TCHAR* Label, int32 SkipTrackingId) -> int32
	{
		// Execute-and-remove each current entry except SkipTrackingId (a cancelled
		// request whose LATE callback we simulate later in the test).
		TArray<TPair<int32, FDesignHttpClientSlotSimulatedCompletion>> Entries;
		for (const TPair<int32, FDesignHttpClientSlotSimulatedCompletion>& Pair
			: HookState->PendingByTrackingId)
		{
			if (Pair.Key != SkipTrackingId)
			{
				Entries.Add(Pair);
			}
		}
		for (const auto& Entry : Entries)
		{
			HookState->PendingByTrackingId.Remove(Entry.Key);
			Entry.Value(true, TEXT("{}"), 200, TEXT("session"));
		}
		Test.AddInfo(FString::Printf(TEXT("%s drained %d callbacks"), Label, Entries.Num()));
		return Entries.Num();
	};

	// Session 1: handshake, then a tool call that gets cancelled.
	Client.ConnectAsync(TEXT("http://phase24.test"), [](bool){});
	DrainPending(*this, TEXT("session-1 handshake"), INDEX_NONE);
	bool bOldCallbackFired = false;
	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(),
		[&bOldCallbackFired](bool, const FString&){ bOldCallbackFired = true; });
	const int32 OldRequestTrackingId = HookState->LastStartedTrackingId;
	Client.CancelActiveRequest();

	// Disconnect + reconnect. With monotonic ids the new requests must NOT reuse
	// OldRequestTrackingId.
	Client.Disconnect();
	Client.ConnectAsync(TEXT("http://phase24.test"), [](bool){});
	int32 HandshakeEntries = DrainPending(*this, TEXT("reconnect handshake"), OldRequestTrackingId);
	HandshakeEntries += DrainPending(*this, TEXT("reconnect handshake notify"), OldRequestTrackingId);
	// Both handshake steps (initialize + notification) must complete despite
	// reused-id paranoia; the cancelled session-1 entry stays pending untouched.
	TestTrue(TEXT("Reconnect handshake completed"), HandshakeEntries >= 2);
	TestFalse(TEXT("Cancelled session-1 entry was not consumed by reconnect drains"),
		HookState->PendingByTrackingId.Find(OldRequestTrackingId) == nullptr);
	TestTrue(TEXT("New session ids are monotonic (no id reuse)"),
		HookState->LastStartedTrackingId > OldRequestTrackingId);

	// New session's live request completes normally...
	FString NewResultPayload;
	bool bNewCallbackFired = false;
	Client.CallToolAsync(TEXT("get_variables"), MakeShared<FJsonObject>(),
		[&bNewCallbackFired, &NewResultPayload](bool bSuccess, const FString& Result)
		{
			bNewCallbackFired = bSuccess;
			NewResultPayload = Result;
		});
	const int32 NewRequestTrackingId = HookState->LastStartedTrackingId;

	// ...and only then does the OLD request's late HTTP callback arrive with a
	// valid-looking payload. It must be recognized as cancelled and swallowed:
	// neither its completion runs, nor is the new request's tracking disturbed.
	const FDesignHttpClientSlotSimulatedCompletion* LateOldCallback =
		HookState->PendingByTrackingId.Find(OldRequestTrackingId);
	if (!TestNotNull(TEXT("Late old-session callback is still registered"), LateOldCallback))
	{
		ResetDesignHttpClientSlotAutomationHooks();
		return false;
	}
	(*LateOldCallback)(true,
		TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"stale\"}]}}\n\n"),
		200, TEXT(""));
	TestFalse(TEXT("Late stale callback did not fire its completion"), bOldCallbackFired);

	// Deliver the new request's own response; it must be unaffected by the late
	// stale callback (active-tracking state was not stomped).
	HookState->PendingByTrackingId[NewRequestTrackingId](true,
		TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"live payload\"}]}}\n\n"),
		200, TEXT(""));
	TestTrue(TEXT("New request still delivers after the late stale callback"),
		bNewCallbackFired && NewResultPayload.Contains(TEXT("live payload")));

	ResetDesignHttpClientSlotAutomationHooks();
	return true;
}
