// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

struct FFigmaHttpClientAutomationHooks
{
	TFunction<bool(
		const FString& Url,
		const FString& Token,
		int32 RequestId,
		TFunction<void(bool bConnected, const FString& ResponseBody, int32 ResponseCode)> OnComplete)> StartRequest;
	TFunction<void(int32 RequestId)> CancelRequest;
};

struct FMCPHttpClientAutomationHooks
{
	TFunction<bool(
		const FString& Method,
		const FString& RequestBody,
		int32 RequestId,
		TFunction<void(bool bConnected, const FString& ResponseBody, int32 ResponseCode, const FString& NewSessionId)> OnComplete)> StartAsyncRequest;
	TFunction<void(int32 RequestId)> CancelRequest;
};

void SetFigmaHttpClientAutomationHooks(const TSharedPtr<FFigmaHttpClientAutomationHooks>& InHooks);
void ResetFigmaHttpClientAutomationHooks();

void SetMCPHttpClientAutomationHooks(const TSharedPtr<FMCPHttpClientAutomationHooks>& InHooks);
void ResetMCPHttpClientAutomationHooks();

#endif
