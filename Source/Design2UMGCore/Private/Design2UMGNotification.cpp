// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGNotification.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	FDesign2UMGSuccessNotificationHandler GSuccessHandler;
}

void FDesign2UMGNotification::RegisterSuccessHandler(FDesign2UMGSuccessNotificationHandler InHandler)
{
	GSuccessHandler = MoveTemp(InHandler);
}

void FDesign2UMGNotification::ResetSuccessHandler()
{
	GSuccessHandler.Unbind();
}

void FDesign2UMGNotification::Success(const FText& Message, const FString& AssetPath)
{
	if (GSuccessHandler.IsBound())
	{
		GSuccessHandler.Execute(Message, AssetPath);
		return;
	}

	FNotificationInfo Info(Message);
	Info.ExpireDuration = 4.0f;
	Info.bUseSuccessFailIcons = true;

	if (auto Item = FSlateNotificationManager::Get().AddNotification(Info))
		Item->SetCompletionState(SNotificationItem::CS_Success);
}

void FDesign2UMGNotification::Warning(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 5.0f;
	Info.bUseSuccessFailIcons = true;

	if (auto Item = FSlateNotificationManager::Get().AddNotification(Info))
		Item->SetCompletionState(SNotificationItem::CS_None);
}

void FDesign2UMGNotification::Error(const FText& Message)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 6.0f;
	Info.bUseSuccessFailIcons = true;

	Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
	{
		FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
	});
	Info.HyperlinkText = NSLOCTEXT("Design2UMG", "OpenOutputLog", "Open Output Log");

	if (auto Item = FSlateNotificationManager::Get().AddNotification(Info))
		Item->SetCompletionState(SNotificationItem::CS_Fail);
}

