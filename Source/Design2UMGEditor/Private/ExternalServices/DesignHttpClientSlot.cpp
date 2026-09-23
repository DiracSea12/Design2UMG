// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignHttpClientSlot.h"

int32 FDesignHttpClientSlot::BeginRequest()
{
	ActiveRequestId = ++NextRequestId;
	return ActiveRequestId;
}

bool FDesignHttpClientSlot::ProcessRequest(const FHttpRequestPtr& Request)
{
	ActiveRequest = Request;
	return Request->ProcessRequest();
}

void FDesignHttpClientSlot::CancelActiveRequest()
{
	if (ActiveRequestId == 0)
	{
		return;
	}

	CancelledRequestIdWatermark = FMath::Max(CancelledRequestIdWatermark, ActiveRequestId);
	if (ActiveRequest.IsValid())
	{
		ActiveRequest->CancelRequest();
	}
	ActiveRequest.Reset();
	ActiveRequestId = 0;
}

bool FDesignHttpClientSlot::ConsumeCompletion(int32 RequestId)
{
	if (ActiveRequestId == RequestId)
	{
		ActiveRequest.Reset();
		ActiveRequestId = 0;
	}
	return RequestId > CancelledRequestIdWatermark;
}

void FDesignHttpClientSlot::ClearActiveRequest(int32 RequestId)
{
	if (ActiveRequestId == RequestId)
	{
		ActiveRequest.Reset();
		ActiveRequestId = 0;
	}
}
