// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#include "Design2UMGNotification.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetRegistryModule.h"

void FDesign2UMGNotification::Success(const FText& Message, const FString& AssetPath)
{
	FNotificationInfo Info(Message);
	Info.ExpireDuration = 4.0f;
	Info.bUseSuccessFailIcons = true;

	if (!AssetPath.IsEmpty())
	{
		Info.Hyperlink = FSimpleDelegate::CreateLambda([AssetPath]()
		{
			// 用 AssetRegistry 获取 FAssetData，避免同步加载完整对象
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			FAssetData AD = ARM.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
			TArray<FAssetData> Assets;
			if (AD.IsValid())
				Assets.Add(AD);
			FContentBrowserModule& CB = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			CB.Get().SyncBrowserToAssets(Assets);
		});
		Info.HyperlinkText = NSLOCTEXT("Design2UMG", "GoToAsset", "Show in Content Browser");
	}

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
