// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportSupport.h"

#include "Design2UMGLog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/FileHelper.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace Design2UMGEditorImportSupport
{
	void ShowNotification(const FText& Text, const SNotificationItem::ECompletionState State)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(State);
		}
	}

	bool LoadTextFileOrLog(const FString& FilePath, FString& OutContents)
	{
		if (FFileHelper::LoadFileToString(OutContents, *FilePath))
		{
			return true;
		}

		UE_LOG(LogDesign2UMG, Error, TEXT("Failed to read file: %s"), *FilePath);
		return false;
	}
}
