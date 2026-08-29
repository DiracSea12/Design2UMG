// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

#if WITH_DEV_AUTOMATION_TESTS
/** Neutral simulated-transport completion shape shared by the HTTP clients. */
using FDesignHttpClientSlotSimulatedCompletion =
	TFunction<void(bool bConnected, const FString& ResponseBody, int32 ResponseCode, const FString& NewSessionId)>;

/**
 * Shared automation injection point for the single-flight HTTP clients.
 * While installed it replaces real HTTP with a simulated transport.
 */
struct FDesignHttpClientSlotAutomationHooks
{
	/**
	 * Simulated transport start for a request. Return false to force the
	 * synchronous launch-failure path. Protocol adapters shape their payload
	 * handling around the neutral completion.
	 */
	TFunction<bool(int32 RequestId, FDesignHttpClientSlotSimulatedCompletion OnComplete)> StartRequest;
	/** Notifies the simulated transport that a request was cancelled. */
	TFunction<void(int32 RequestId)> CancelRequest;
};

void SetDesignHttpClientSlotAutomationHooks(const TSharedPtr<FDesignHttpClientSlotAutomationHooks>& InHooks);
void ResetDesignHttpClientSlotAutomationHooks();
#endif

/**
 * Single-flight HTTP request slot shared by the HTTP clients: at most one
 * active request per client, starting a new request silently cancels the
 * previous one, and completions belonging to a cancelled request are
 * recognized and swallowed. Not thread-safe on its own; embed it in the
 * client's shared state like the request fields it replaces.
 */
class FDesignHttpClientSlot
{
public:
	/** Reserves the next request id and makes it the active request. */
	int32 BeginRequest();

	/** Binds the real HTTP request backing the active request id. */
	void BindActiveRequest(const FHttpRequestPtr& Request);

	/** Cancels the active request (real or simulated) and forgets it. */
	void CancelActiveRequest();

	/**
	 * Completion bookkeeping for RequestId: clears the active request when it
	 * is still current and reports whether the completion is live. A
	 * completion with an id at or below the cancellation watermark belonged to
	 * a cancelled request and must be swallowed.
	 */
	bool ConsumeCompletion(int32 RequestId);

	/** Clears the active request after a synchronous launch failure. */
	void ClearActiveRequest(int32 RequestId);

private:
	FHttpRequestPtr ActiveRequest;
	int32 ActiveRequestId = 0;
	int32 NextRequestId = 0;
	/** Ids are monotonic and single-flight, so any id <= watermark was cancelled. */
	int32 CancelledRequestIdWatermark = 0;
};

#if WITH_DEV_AUTOMATION_TESTS
namespace DesignHttpClientSlot
{
	/** True while a simulated transport is installed and can start requests. */
	bool HasAutomationTransport();

	/** Routes a request through the installed simulated transport. */
	bool StartAutomationTransport(int32 RequestId, FDesignHttpClientSlotSimulatedCompletion&& OnComplete);
}
#endif
