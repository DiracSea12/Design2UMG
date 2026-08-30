// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace Design2UMGEditorImportSupport
{
	void ShowNotification(const FText& Text, const SNotificationItem::ECompletionState State);
	bool LoadTextFileOrLog(const FString& FilePath, FString& OutContents);
}
