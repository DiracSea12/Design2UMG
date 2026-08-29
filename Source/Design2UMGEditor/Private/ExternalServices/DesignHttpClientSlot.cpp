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

void FDesignHttpClientSlot::BindActiveRequest(const FHttpRequestPtr& Request)
{
	ActiveRequest = Request;
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
#if WITH_DEV_AUTOMATION_TESTS
	else if (GDesignHttpClientSlotAutomationHooks.IsValid() && GDesignHttpClientSlotAutomationHooks->CancelRequest)
	{
		GDesignHttpClientSlotAutomationHooks->CancelRequest(ActiveRequestId);
	}
#endif
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
namespace DesignHttpClientSlot
{
	bool HasAutomationTransport()
	{
		return GDesignHttpClientSlotAutomationHooks.IsValid()
			&& GDesignHttpClientSlotAutomationHooks->StartRequest;
	}

	bool StartAutomationTransport(int32 RequestId, FDesignHttpClientSlotSimulatedCompletion&& OnComplete)
	{
		if (!GDesignHttpClientSlotAutomationHooks.IsValid() || !GDesignHttpClientSlotAutomationHooks->StartRequest)
		{
			return false;
		}
		return GDesignHttpClientSlotAutomationHooks->StartRequest(RequestId, MoveTemp(OnComplete));
	}
}

void SetDesignHttpClientSlotAutomationHooks(const TSharedPtr<FDesignHttpClientSlotAutomationHooks>& InHooks)
{
	GDesignHttpClientSlotAutomationHooks = InHooks;
}

void ResetDesignHttpClientSlotAutomationHooks()
{
	GDesignHttpClientSlotAutomationHooks.Reset();
}
#endif
