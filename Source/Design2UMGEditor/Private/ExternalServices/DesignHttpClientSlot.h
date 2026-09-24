// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Interfaces/IHttpRequest.h"
#include "HttpModule.h"

/**
 * One active HTTP request per client. Replacement cancels the previous request;
 * the cancellation watermark suppresses late completions. Caller serializes access.
 */
class FDesignHttpClientSlot
{
public:
	explicit FDesignHttpClientSlot(FHttpModule& InHttpModule) : HttpModule(InHttpModule) {}
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> CreateRequest() { return HttpModule.CreateRequest(); }

	/** Reserves the next request id and makes it the active request. */
	int32 BeginRequest();

	/** Starts the configured request through its transport. */
	bool ProcessRequest(const FHttpRequestPtr& Request);

	/** Cancels the active request and forgets it. */
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
	FHttpModule& HttpModule;
	FHttpRequestPtr ActiveRequest;
	int32 ActiveRequestId = 0;
	int32 NextRequestId = 0;
	/** Ids are monotonic and single-flight, so any id <= watermark was cancelled. */
	int32 CancelledRequestIdWatermark = 0;
};
