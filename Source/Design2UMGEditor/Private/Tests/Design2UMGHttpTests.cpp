// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGHttpTestTransport.h"
#include "FigmaHttpClient.h"
#include "MCPHttpClient.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
using Design2UMGEditorTests::FHttpTestResponse;
using Design2UMGEditorTests::FScopedHttpTestTransport;

namespace
{
	const TCHAR* LiveToolResponse = TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"live payload\"}]}}\n\n");
	const TCHAR* StaleToolResponse = TEXT("event: message\ndata: {\"jsonrpc\":\"2.0\",\"result\":{\"content\":[{\"type\":\"text\",\"text\":\"stale\"}]}}\n\n");

	bool CompleteHandshake(FAutomationTestBase& Test, FScopedHttpTestTransport& Transport)
	{
		const int32 InitializeId = Transport.LastStartedId;
		if (!Test.TestTrue(TEXT("Initialize request is pending"), Transport.Pending.Contains(InitializeId))) return false;
		Transport.Complete(InitializeId, TEXT("{}"), 200, {{TEXT("Mcp-Session-Id"), TEXT("test-session")}});
		const int32 NotificationId = Transport.LastStartedId;
		if (!Test.TestTrue(TEXT("Initialize chains a separate notification"),
			NotificationId != InitializeId && Transport.Pending.Contains(NotificationId))) return false;
		Transport.Complete(NotificationId, TEXT(""), 202);
		return true;
	}
}

// Existing identifiers stay stable while their fixtures and source ownership change.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDFigmaHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.FigmaHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDFigmaHttpCancellationBehaviorTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client;
	int32 CallbackCount = 0;
	Client.GetFile(TEXT("test-file"), [&](bool, const FString&) { ++CallbackCount; });
	TestEqual(TEXT("One request started"), Transport.Pending.Num(), 1);
	Client.CancelRequests();
	TestEqual(TEXT("One cancellation is delivered to transport"), Transport.CancelledIds.Num(), 1);
	Transport.Complete(Transport.LastStartedId, false, nullptr);
	TestEqual(TEXT("Cancelled request suppresses late completion"), CallbackCount, 0);

	AddExpectedError(TEXT("FigmaHttpClient: ProcessRequest failed"), EAutomationExpectedErrorFlags::Contains, 1);
	Transport.bStartRequests = false;
	bool bLastSuccess = true;
	Client.GetFile(TEXT("test-file"), [&](bool bSuccess, const FString& Body)
	{
		++CallbackCount;
		bLastSuccess = bSuccess;
		TestTrue(TEXT("Launch failure has no response body"), Body.IsEmpty());
	});
	TestEqual(TEXT("Launch failure completes exactly once"), CallbackCount, 1);
	TestFalse(TEXT("Launch failure reports failure"), bLastSuccess);
	Client.CancelRequests();
	TestEqual(TEXT("Failed launch leaves no active request to cancel"), Transport.CancelledIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpCancellationBehaviorTest,
	"Design2UMG.BatchD.MCPHttpCancellationBehavior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpCancellationBehaviorTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	bool bConnected = false;
	Client.ConnectAsync(TEXT("http://transport.test"), [&](bool bSuccess) { bConnected = bSuccess; });
	if (!CompleteHandshake(*this, Transport)) return false;
	TestTrue(TEXT("Async connect completes"), bConnected);
	bool bCallbackCalled = false;
	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(), [&](bool, const FString&) { bCallbackCalled = true; });
	Client.CancelRequests();
	TestEqual(TEXT("Cancellation reaches transport once"), Transport.CancelledIds.Num(), 1);
	Transport.Complete(Transport.LastStartedId, LiveToolResponse);
	TestFalse(TEXT("Cancelled MCP request suppresses late callback"), bCallbackCalled);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPHttpClientSessionCycleReleaseTest,
	"Design2UMG.BatchD.MCPHttpClientSessionCycleRelease",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPHttpClientSessionCycleReleaseTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	bool bConnected = false;
	Client.ConnectAsync(TEXT("http://transport.test"), [&](bool bSuccess) { bConnected = bSuccess; });
	const auto WeakSession = Client.GetSessionWeakForTests();
	if (!TestTrue(TEXT("Connect creates a session"), WeakSession.IsValid()) || !CompleteHandshake(*this, Transport)) return false;
	TestTrue(TEXT("Both handshake steps complete"), bConnected);
	TestTrue(TEXT("Completed requests no longer retain callbacks"), Transport.Pending.IsEmpty());
	TestTrue(TEXT("Client owns the connected session"), WeakSession.IsValid());
	Client.Disconnect();
	TestFalse(TEXT("Disconnect releases the session, without a callback ownership cycle"), WeakSession.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPRequestIdSemanticsTest,
	"Design2UMG.BatchD.MCPRequestIdSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPRequestIdSemanticsTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	Client.ConnectAsync(TEXT("http://transport.test"), [](bool) {});
	if (!CompleteHandshake(*this, Transport)) return false;
	TestTrue(TEXT("Handshake has no pending requests"), Transport.Pending.IsEmpty());

	int32 ACallbackCount = 0;
	bool bASuccess = true;
	FString BPayload;
	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(), [&](bool bSuccess, const FString&)
	{
		++ACallbackCount;
		bASuccess = bSuccess;
	});
	const int32 RequestA = Transport.LastStartedId;
	Client.CallToolAsync(TEXT("get_variables"), MakeShared<FJsonObject>(), [&](bool bSuccess, const FString& Body)
	{
		TestTrue(TEXT("Replacement response succeeds"), bSuccess);
		BPayload = Body;
	});
	const int32 RequestB = Transport.LastStartedId;
	TestTrue(TEXT("Replacing a request cancels the old one"), Transport.CancelledIds.Contains(RequestA));
	TestTrue(TEXT("Replacement uses a new tracking id"), RequestB > RequestA);
	TestEqual(TEXT("Superseded request receives exactly one callback"), ACallbackCount, 1);
	TestFalse(TEXT("Superseded request reports failure"), bASuccess);
	Transport.Complete(RequestA, StaleToolResponse);
	TestEqual(TEXT("Late response cannot deliver a second callback"), ACallbackCount, 1);
	Transport.Complete(RequestB, LiveToolResponse);
	TestEqual(TEXT("Live replacement delivers its own payload"), BPayload, FString(TEXT("live payload")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBatchDMCPDisconnectCollisionTest,
	"Design2UMG.BatchD.MCPDisconnectCollision",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FBatchDMCPDisconnectCollisionTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	int32 OldConnectionCallbacks = 0;
	Client.ConnectAsync(TEXT("http://transport.test"), [&](bool) { ++OldConnectionCallbacks; });
	// Leave the old notification pending: a tool can supersede it after initialize.
	Transport.Complete(Transport.LastStartedId, TEXT("{}"), 200, {{TEXT("Mcp-Session-Id"), TEXT("old-session")}});
	const int32 OldNotificationId = Transport.LastStartedId;
	if (!TestTrue(TEXT("Old initialized notification is pending"), Transport.Pending.Contains(OldNotificationId))) return false;
	bool bOldCallbackFired = false;
	Client.CallToolAsync(TEXT("batch_get"), MakeShared<FJsonObject>(), [&](bool, const FString&) { bOldCallbackFired = true; });
	const int32 OldId = Transport.LastStartedId;
	TestTrue(TEXT("Tool request supersedes the old notification"), Transport.CancelledIds.Contains(OldNotificationId));
	Client.CancelActiveRequest();
	Client.Disconnect();

	const int32 OldCallbacksBeforeReconnect = OldConnectionCallbacks;
	bool bReconnected = false;
	Client.ConnectAsync(TEXT("http://transport.test"), [&](bool bSuccess) { bReconnected = bSuccess; });
	const int32 NewInitializeId = Transport.LastStartedId;
	Transport.Complete(OldNotificationId, TEXT(""), 202);
	TestEqual(TEXT("Late old notification does not complete the old connection again"), OldConnectionCallbacks, OldCallbacksBeforeReconnect);
	TestFalse(TEXT("Late old notification cannot complete the new connection"), bReconnected);
	TestTrue(TEXT("New initialize remains pending after stale notification"), Transport.Pending.Contains(NewInitializeId));
	if (!CompleteHandshake(*this, Transport)) return false;
	TestTrue(TEXT("Reconnect completes both handshake steps"), bReconnected);
	TestTrue(TEXT("New session tracking ids cannot collide with the previous session"), Transport.LastStartedId > OldId);
	if (!TestTrue(TEXT("Old cancelled request remains available for late delivery"), Transport.Pending.Contains(OldId))) return false;
	FString NewPayload;
	Client.CallToolAsync(TEXT("get_variables"), MakeShared<FJsonObject>(), [&](bool bSuccess, const FString& Body)
	{
		TestTrue(TEXT("New session response succeeds"), bSuccess);
		NewPayload = Body;
	});
	const int32 NewId = Transport.LastStartedId;
	Transport.Complete(OldId, StaleToolResponse);
	TestFalse(TEXT("Old session late response is suppressed"), bOldCallbackFired);
	Transport.Complete(NewId, LiveToolResponse);
	TestEqual(TEXT("Late old response does not disturb the active request"), NewPayload, FString(TEXT("live payload")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaHttpResponseContractTest,
	"Design2UMG.Http.Figma.ResponseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaHttpResponseContractTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client;
	Client.SetToken(TEXT("test-token"));
	AddExpectedError(TEXT("FigmaHttpClient: HTTP "), EAutomationExpectedErrorFlags::Contains, 5);
	AddExpectedError(TEXT("FigmaHttpClient: Connection failed"), EAutomationExpectedErrorFlags::Contains, 2);
	const FString Body = TEXT("{\"name\":\"响应内容\"}");
	for (const int32 Code : {199, 200, 299, 300, 401, 429, 503})
	{
		int32 Callbacks = 0;
		Client.GetFile(TEXT("test-file"), [&](bool bSuccess, const FString& Response)
		{
			++Callbacks;
			TestEqual(*FString::Printf(TEXT("HTTP %d success contract"), Code), bSuccess, Code >= 200 && Code < 300);
			TestEqual(TEXT("Figma preserves response body even for HTTP failure"), Response, Body);
		});
		const FHttpRequestPtr Request = Transport.Pending.FindChecked(Transport.LastStartedId);
		TestEqual(TEXT("Real request verb is GET"), Request->GetVerb(), FString(TEXT("GET")));
		TestEqual(TEXT("Real request uses the configured token"), Request->GetHeader(TEXT("X-Figma-Token")), FString(TEXT("test-token")));
		TestTrue(TEXT("Real request targets the file endpoint"), Request->GetURL().EndsWith(TEXT("/v1/files/test-file?geometry=paths")));
		Transport.Complete(Transport.LastStartedId, Body, Code);
		TestEqual(TEXT("Each HTTP response completes once"), Callbacks, 1);
	}
	for (const bool bConnected : {false, true})
	{
		int32 Callbacks = 0;
		Client.GetFile(TEXT("test-file"), [&](bool bSuccess, const FString& Response)
		{
			++Callbacks;
			TestFalse(TEXT("Connection failure or absent response fails"), bSuccess);
			TestTrue(TEXT("Absent response has no body"), Response.IsEmpty());
		});
		Transport.Complete(Transport.LastStartedId, bConnected, nullptr);
		TestEqual(TEXT("Missing response completes once"), Callbacks, 1);
	}
	Client.CancelRequests();
	TestTrue(TEXT("Completed requests leave no active cancellation"), Transport.CancelledIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPHttpResponseContractTest,
	"Design2UMG.Http.MCP.ResponseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPHttpResponseContractTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	Client.ConnectAsync(TEXT("http://transport.test"), [](bool) {});
	if (!CompleteHandshake(*this, Transport)) return false;
	AddExpectedError(TEXT("MCPHttpClient: Async parse failed"), EAutomationExpectedErrorFlags::Contains, 1);
	struct FCase { bool bConnected; bool bHasResponse; int32 Code; const TCHAR* Body; bool bSuccess; };
	const FCase Cases[] = {
		{true, true, 200, LiveToolResponse, true},
		{true, true, 299, LiveToolResponse, true},
		{true, true, 300, LiveToolResponse, false},
		{true, true, 503, LiveToolResponse, false},
		{false, true, 200, LiveToolResponse, false},
		{true, false, 200, LiveToolResponse, false},
		{true, true, 200, TEXT("not json"), false},
	};
	for (const FCase& Case : Cases)
	{
		int32 Callbacks = 0;
		Client.CallToolAsync(TEXT("example_tool"), MakeShared<FJsonObject>(), [&](bool bSuccess, const FString& Response)
		{
			++Callbacks;
			TestEqual(TEXT("Tool response success contract"), bSuccess, Case.bSuccess);
			TestEqual(TEXT("Only parsed success returns a tool payload"), Response, Case.bSuccess ? FString(TEXT("live payload")) : FString());
		});
		const FHttpRequestPtr Request = Transport.Pending.FindChecked(Transport.LastStartedId);
		TestEqual(TEXT("Real MCP request is POST"), Request->GetVerb(), FString(TEXT("POST")));
		TestEqual(TEXT("Session header propagates into real tool request"), Request->GetHeader(TEXT("Mcp-Session-Id")), FString(TEXT("test-session")));
		const FHttpResponsePtr Response = Case.bHasResponse
			? MakeShared<FHttpTestResponse, ESPMode::ThreadSafe>(Request->GetURL(), Case.Body, Case.Code)
			: FHttpResponsePtr();
		Transport.Complete(Transport.LastStartedId, Case.bConnected, Response);
		TestEqual(TEXT("Tool request completes exactly once"), Callbacks, 1);
	}
	Client.CancelRequests();
	TestTrue(TEXT("Success and failures release the active request"), Transport.CancelledIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPHttpHandshakeFailureTest,
	"Design2UMG.Http.MCP.HandshakeFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPHttpHandshakeFailureTest::RunTest(const FString&)
{
	AddExpectedError(TEXT("MCPHttpClient: ProcessRequest failed"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("MCPHttpClient: Connection failed"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("MCPHttpClient: HTTP 503"), EAutomationExpectedErrorFlags::Contains, 2);
	AddExpectedError(TEXT("MCPHttpClient: async initialize handshake failed"), EAutomationExpectedErrorFlags::Contains, 4);
	AddExpectedError(TEXT("MCPHttpClient: No Mcp-Session-Id"), EAutomationExpectedErrorFlags::Contains, 1);
	struct FCase { bool bStarts; bool bConnected; bool bHasResponse; int32 Code; };
	const FCase Cases[] = {
		{false, false, false, 0}, {true, false, false, 0},
		{true, true, false, 0}, {true, true, true, 503}, {true, true, true, 200},
	};
	for (const FCase& Case : Cases)
	{
		FScopedHttpTestTransport Transport;
		Transport.bStartRequests = Case.bStarts;
		FMCPHttpClient Client;
		int32 Callbacks = 0;
		Client.ConnectAsync(TEXT("http://transport.test"), [&](bool bSuccess)
		{
			++Callbacks;
			TestFalse(TEXT("Incomplete handshake reports failure"), bSuccess);
		});
		if (Case.bStarts)
		{
			const FHttpResponsePtr Response = Case.bHasResponse
				? MakeShared<FHttpTestResponse, ESPMode::ThreadSafe>(TEXT("http://transport.test"), TEXT("{}"), Case.Code)
				: FHttpResponsePtr();
			Transport.Complete(Transport.LastStartedId, Case.bConnected, Response);
		}
		TestEqual(TEXT("Handshake failure completes exactly once"), Callbacks, 1);
		TestFalse(TEXT("Failed initialize does not establish a session"), Client.IsConnected());
	}

	// Notification failure is a warning after a successful initialize, not a lost session.
	FScopedHttpTestTransport Transport;
	FMCPHttpClient Client;
	int32 Callbacks = 0;
	Client.ConnectAsync(TEXT("http://transport.test"), [&](bool bSuccess)
	{
		++Callbacks;
		TestTrue(TEXT("Established session survives notification failure"), bSuccess);
	});
	Transport.Complete(Transport.LastStartedId, TEXT("{}"), 200, {{TEXT("Mcp-Session-Id"), TEXT("test-session")}});
	Transport.Complete(Transport.LastStartedId, TEXT(""), 503);
	TestEqual(TEXT("Notification failure completes connection once"), Callbacks, 1);
	TestTrue(TEXT("Session remains connected"), Client.IsConnected());
	return true;
}
#endif
