// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignHttpClientSlot.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	TSharedPtr<FDesignHttpClientSlotAutomationHooks> GDesignHttpClientSlotAutomationHooks;
}
#endif

int32 FDesignHttpClientSlot::BeginRequest()
{
	ActiveRequestId = ++NextRequestId;
	return ActiveRequestId;
}

bool FDesignHttpClientSlot::ProcessRequest(const FHttpRequestPtr& Request)
{
	ActiveRequest = Request;
#if WITH_DEV_AUTOMATION_TESTS
	if (GDesignHttpClientSlotAutomationHooks.IsValid())
	{
		return GDesignHttpClientSlotAutomationHooks->StartRequest(ActiveRequestId, Request);
	}
#endif
	return Request->ProcessRequest();
}

void FDesignHttpClientSlot::CancelActiveRequest()
{
	if (ActiveRequestId == 0)
	{
		return;
	}

	CancelledRequestIdWatermark = FMath::Max(CancelledRequestIdWatermark, ActiveRequestId);
#if WITH_DEV_AUTOMATION_TESTS
	if (GDesignHttpClientSlotAutomationHooks.IsValid())
	{
		if (GDesignHttpClientSlotAutomationHooks->CancelRequest)
		{
			GDesignHttpClientSlotAutomationHooks->CancelRequest(ActiveRequestId);
		}
	}
	else
#endif
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

#if WITH_DEV_AUTOMATION_TESTS
void SetDesignHttpClientSlotAutomationHooks(const TSharedPtr<FDesignHttpClientSlotAutomationHooks>& InHooks)
{
	GDesignHttpClientSlotAutomationHooks = InHooks;
}

void ResetDesignHttpClientSlotAutomationHooks()
{
	GDesignHttpClientSlotAutomationHooks.Reset();
}
#endif
