// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"

DECLARE_DELEGATE_TwoParams(FDesign2UMGSuccessNotificationHandler, const FText&, const FString&);
DECLARE_DELEGATE_OneParam(FDesign2UMGMessageNotificationHandler, const FText&);

struct DESIGN2UMGCORE_API FDesign2UMGNotification
{
	static void RegisterSuccessHandler(FDesign2UMGSuccessNotificationHandler InHandler);
	static void RegisterWarningHandler(FDesign2UMGMessageNotificationHandler InHandler);
	static void RegisterErrorHandler(FDesign2UMGMessageNotificationHandler InHandler);
	static void ResetHandlers();

	static void Success(const FText& Message, const FString& AssetPath = TEXT(""));
	static void Warning(const FText& Message);
	static void Error(const FText& Message);
};
