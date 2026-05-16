// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGNotification.h"
#include "Design2UMGLog.h"

namespace
{
	FDesign2UMGSuccessNotificationHandler GSuccessHandler;
	FDesign2UMGMessageNotificationHandler GWarningHandler;
	FDesign2UMGMessageNotificationHandler GErrorHandler;

	enum class ENotificationFallbackVerbosity : uint8
	{
		Success,
		Warning,
		Error,
	};

	void LogNotificationFallback(
		const ENotificationFallbackVerbosity Verbosity,
		const TCHAR* Prefix,
		const FText& Message,
		const FString* AssetPath = nullptr)
	{
		const FString Suffix = (AssetPath != nullptr && !AssetPath->IsEmpty())
			? FString::Printf(TEXT(" [%s]"), **AssetPath)
			: FString();
		const FString Text = FString::Printf(TEXT("%s%s%s"), Prefix, *Message.ToString(), *Suffix);
		switch (Verbosity)
		{
		case ENotificationFallbackVerbosity::Error:
			UE_LOG(LogDesign2UMG, Error, TEXT("%s"), *Text);
			break;
		case ENotificationFallbackVerbosity::Warning:
			UE_LOG(LogDesign2UMG, Warning, TEXT("%s"), *Text);
			break;
		default:
			UE_LOG(LogDesign2UMG, Display, TEXT("%s"), *Text);
			break;
		}
	}
}

void FDesign2UMGNotification::RegisterSuccessHandler(FDesign2UMGSuccessNotificationHandler InHandler)
{
	GSuccessHandler = MoveTemp(InHandler);
}

void FDesign2UMGNotification::RegisterWarningHandler(FDesign2UMGMessageNotificationHandler InHandler)
{
	GWarningHandler = MoveTemp(InHandler);
}

void FDesign2UMGNotification::RegisterErrorHandler(FDesign2UMGMessageNotificationHandler InHandler)
{
	GErrorHandler = MoveTemp(InHandler);
}

void FDesign2UMGNotification::ResetHandlers()
{
	GSuccessHandler.Unbind();
	GWarningHandler.Unbind();
	GErrorHandler.Unbind();
}

void FDesign2UMGNotification::Success(const FText& Message, const FString& AssetPath)
{
	if (GSuccessHandler.IsBound())
	{
		GSuccessHandler.Execute(Message, AssetPath);
		return;
	}

	LogNotificationFallback(ENotificationFallbackVerbosity::Success, TEXT("Success: "), Message, &AssetPath);
}

void FDesign2UMGNotification::Warning(const FText& Message)
{
	if (GWarningHandler.IsBound())
	{
		GWarningHandler.Execute(Message);
		return;
	}

	LogNotificationFallback(ENotificationFallbackVerbosity::Warning, TEXT("Warning: "), Message);
}

void FDesign2UMGNotification::Error(const FText& Message)
{
	if (GErrorHandler.IsBound())
	{
		GErrorHandler.Execute(Message);
		return;
	}

	LogNotificationFallback(ENotificationFallbackVerbosity::Error, TEXT("Error: "), Message);
}

