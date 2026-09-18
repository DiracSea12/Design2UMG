// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"

#if WITH_DEV_AUTOMATION_TESTS
/**
 * Shared automation injection point for the single-flight HTTP clients.
 * While installed it replaces real HTTP with a simulated transport.
 */
struct FDesignHttpClientSlotAutomationHooks
{
	/**
	 * Simulated transport start for a request. Return false to force the
	 * synchronous launch-failure path. Complete the supplied request through
	 * its real OnProcessRequestComplete delegate; do not reimplement response handling.
	 */
	TFunction<bool(int32 RequestId, const FHttpRequestPtr& Request)> StartRequest;
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

	/** Starts the configured request through HTTP or the installed test transport. */
	bool ProcessRequest(const FHttpRequestPtr& Request);

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
