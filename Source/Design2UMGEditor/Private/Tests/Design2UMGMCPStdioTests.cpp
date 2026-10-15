// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Tests/Design2UMGMCPStdioTestProcess.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace Design2UMGEditorTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioCancellationTest, "Design2UMG.MCP.Stdio.Cancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioCancellationTest::RunTest(const FString&)
{
	FMCPStdioClient Client;
	if (!ConnectStdioFixture(*this, Client)) return false;
	int32 Callbacks = 0;
	Client.CallToolAsync(TEXT("slow"), nullptr, [&](bool, const FString&) { ++Callbacks; });
	Client.CancelRequests();
	bool bLiveCompleted = false;
	Client.CallToolAsync(TEXT("live"), nullptr, [&](bool bSuccess, const FString& Text)
	{
		++Callbacks;
		TestTrue(TEXT("Fresh request succeeds after silent cancellation"), bSuccess && Text == TEXT("live"));
		bLiveCompleted = true;
	});
	TestTrue(TEXT("Fresh request completes"), PumpStdioUntil([&]() { return bLiveCompleted; }));
	TestEqual(TEXT("Late cancelled response is discarded"), Callbacks, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioSessionLifetimeTest, "Design2UMG.MCP.Stdio.SessionLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioSessionLifetimeTest::RunTest(const FString&)
{
	int32 Callbacks = 0;
	TSharedPtr<int32> Lifetime = MakeShared<int32>(1);
	TWeakPtr<int32> WeakLifetime = Lifetime;
	{
		FMCPStdioClient Client;
		if (!ConnectStdioFixture(*this, Client)) return false;
		Client.CallToolAsync(TEXT("slow"), nullptr, [Lifetime, &Callbacks](bool, const FString&) { ++Callbacks; });
		Lifetime.Reset();
		TestTrue(TEXT("Pending callback retains its captures"), WeakLifetime.IsValid());
		Client.Disconnect();
		TestFalse(TEXT("Disconnect releases callbacks and their captures"), WeakLifetime.IsValid());
		TestFalse(TEXT("Disconnect closes the child process"), Client.IsRunning());
	}
	TestEqual(TEXT("Disconnect and destruction are silent"), Callbacks, 0);
	{
		FMCPStdioClient Client;
		Client.ConnectAsync(StdioTestPython(), StdioTestArguments(), [&](bool) { ++Callbacks; });
	}
	TestEqual(TEXT("Destruction cancels an unfinished initialize callback"), Callbacks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioRequestIdentityTest, "Design2UMG.MCP.Stdio.RequestIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioRequestIdentityTest::RunTest(const FString&)
{
	FMCPStdioClient Client;
	if (!ConnectStdioFixture(*this, Client)) return false;
	int32 FirstCallbacks = 0, LastCallbacks = 0;
	Client.CallToolAsync(TEXT("slow"), nullptr, [&](bool bSuccess, const FString&) { ++FirstCallbacks; TestFalse(TEXT("Superseded request fails"), bSuccess); });
	const TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
	Arguments->SetStringField(TEXT("label"), TEXT("中文请求"));
	Client.CallToolAsync(TEXT("fragmented"), Arguments, [&](bool bSuccess, const FString& Text)
	{
		++LastCallbacks;
		TestTrue(TEXT("Split UTF-8 survives line framing"), bSuccess && Text == TEXT("中文结果"));
		Client.CallToolAsync(TEXT("reentrant"), nullptr, [&](bool bNextSuccess, const FString& NextText)
		{ ++LastCallbacks; TestTrue(TEXT("Reentrant tool call succeeds"), bNextSuccess && NextText == TEXT("reentrant")); });
	});
	TestTrue(TEXT("Live and reentrant requests finish"), PumpStdioUntil([&]() { return LastCallbacks == 2; }));
	TestEqual(TEXT("Superseded callback fires once despite its late response"), FirstCallbacks, 1);
	int32 CancelledCallbacks = 0, ReplacementCallbacks = 0;
	Client.CallToolAsync(TEXT("slow"), nullptr, [&](bool bSuccess, const FString&)
	{
		++CancelledCallbacks;
		TestFalse(TEXT("Superseded callback receives failure before disconnecting"), bSuccess);
		Client.Disconnect();
	});
	Client.CallToolAsync(TEXT("replacement"), nullptr, [&](bool bSuccess, const FString&)
	{ ++ReplacementCallbacks; TestFalse(TEXT("Reentrant disconnect also fails the replacement"), bSuccess); });
	TestEqual(TEXT("Disconnecting superseded callback runs once"), CancelledCallbacks, 1);
	TestEqual(TEXT("Replacement reports failure once without touching closed pipes"), ReplacementCallbacks, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioReconnectionTest, "Design2UMG.MCP.Stdio.Reconnection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioReconnectionTest::RunTest(const FString&)
{
	FMCPStdioClient Client;
	if (!ConnectStdioFixture(*this, Client)) return false;
	int32 StaleCallbacks = 0;
	Client.CallToolAsync(TEXT("slow"), nullptr, [&](bool, const FString&) { ++StaleCallbacks; });
	if (!ConnectStdioFixture(*this, Client)) return false;
	bool bCompleted = false;
	Client.CallToolAsync(TEXT("new_session"), nullptr, [&](bool bSuccess, const FString& Text)
	{ bCompleted = true; TestTrue(TEXT("Only the new child answers"), bSuccess && Text == TEXT("new_session")); });
	TestTrue(TEXT("Reconnected session completes"), PumpStdioUntil([&]() { return bCompleted; }));
	TestEqual(TEXT("Prior session callbacks remain cancelled"), StaleCallbacks, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioResponseContractTest, "Design2UMG.MCP.Stdio.ResponseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioResponseContractTest::RunTest(const FString&)
{
	FMCPStdioClient Client;
	if (!ConnectStdioFixture(*this, Client)) return false;
	for (const FString& Tool : {TEXT("tool_error"), TEXT("rpc_error"), TEXT("missing_content"), TEXT("exit")})
	{
		bool bCompleted = false;
		int32 Callbacks = 0;
		Client.CallToolAsync(Tool, MakeShared<FJsonObject>(), [&](bool bSuccess, const FString&)
		{ ++Callbacks; bCompleted = true; TestFalse(TEXT("Invalid tool response reports failure"), bSuccess); });
		const bool bFinished = PumpStdioUntil([&]() { return bCompleted; });
		TestTrue(TEXT("Failure completes instead of hanging"), bFinished);
		if (!bFinished) { Client.Disconnect(); return false; }
		TestEqual(TEXT("Failure callback fires once"), Callbacks, 1);
	}
	TestFalse(TEXT("Exited process clears connected state"), Client.IsConnected());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPStdioHandshakeFailureTest, "Design2UMG.MCP.Stdio.HandshakeFailures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMCPStdioHandshakeFailureTest::RunTest(const FString&)
{
	for (const FString& Mode : {TEXT("bad_handshake"), TEXT("malformed"), TEXT("exit")})
	{
		FMCPStdioClient Client;
		bool bCompleted = false;
		int32 Callbacks = 0;
		Client.ConnectAsync(StdioTestPython(), StdioTestArguments(Mode), [&](bool bSuccess)
		{ ++Callbacks; bCompleted = true; TestFalse(TEXT("Invalid handshake fails"), bSuccess); });
		const bool bFinished = PumpStdioUntil([&]() { return bCompleted; });
		TestTrue(TEXT("Invalid handshake finishes"), bFinished);
		if (!bFinished) { Client.Disconnect(); return false; }
		TestEqual(TEXT("Handshake failure is delivered once"), Callbacks, 1);
		TestFalse(TEXT("Failed initialize shuts down the child"), Client.IsRunning());
	}
	FMCPStdioClient Client;
	bool bCalled = false;
	Client.CallToolAsync(TEXT("execute"), nullptr, [&](bool bSuccess, const FString&) { bCalled = true; TestFalse(TEXT("Disconnected call fails"), bSuccess); });
	TestTrue(TEXT("Disconnected call completes immediately"), bCalled);
	return true;
}
#endif
