// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGHttpTestTransport.h"
#include "FigmaHttpClient.h"
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"

#if WITH_DEV_AUTOMATION_TESTS
using Design2UMGEditorTests::FHttpTestResponse;
using Design2UMGEditorTests::FScopedHttpTestTransport;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaHttpCancellationBehaviorTest,
	"Design2UMG.Http.Figma.Cancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaHttpCancellationBehaviorTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFigmaHttpResponseContractTest,
	"Design2UMG.Http.Figma.ResponseContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFigmaHttpResponseContractTest::RunTest(const FString&)
{
	FScopedHttpTestTransport Transport;
	FFigmaHttpClient Client([&Transport]() { return Transport.CreateRequest(); });
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


#endif
