// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"

#include "Design2UMGEditorCommandOwner.h"
#include "Design2UMGEditorImportOrchestrator.h"
#include "Design2UMGEditorMenuOwner.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Engine/Font.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	void ShowEditorNotification(FNotificationInfo&& Info, const SNotificationItem::ECompletionState CompletionState)
	{
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(CompletionState);
		}
	}

	void ShowEditorSuccessNotification(const FText& Message, const FString& AssetPath)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 4.0f;
		Info.bUseSuccessFailIcons = true;

		if (!AssetPath.IsEmpty())
		{
			Info.Hyperlink = FSimpleDelegate::CreateLambda([AssetPath]()
			{
				FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				FAssetData AssetData = AssetRegistry.Get().GetAssetByObjectPath(FSoftObjectPath(AssetPath));
				if (!AssetData.IsValid())
				{
					return;
				}

				TArray<FAssetData> Assets;
				Assets.Add(AssetData);
				FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
				ContentBrowser.Get().SyncBrowserToAssets(Assets);
			});
			Info.HyperlinkText = NSLOCTEXT("Design2UMG", "GoToAsset", "Show in Content Browser");
		}

		ShowEditorNotification(MoveTemp(Info), SNotificationItem::CS_Success);
	}

	void ShowEditorWarningNotification(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.0f;
		Info.bUseSuccessFailIcons = true;
		ShowEditorNotification(MoveTemp(Info), SNotificationItem::CS_None);
	}

	void ShowEditorInfoNotification(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 5.0f;
		ShowEditorNotification(MoveTemp(Info), SNotificationItem::CS_None);
	}

	void ShowEditorErrorNotification(const FText& Message)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 6.0f;
		Info.bUseSuccessFailIcons = true;
		Info.Hyperlink = FSimpleDelegate::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(FName("OutputLog"));
		});
		Info.HyperlinkText = NSLOCTEXT("Design2UMG", "OpenOutputLog", "Open Output Log");
		ShowEditorNotification(MoveTemp(Info), SNotificationItem::CS_Fail);
	}
}

void FDesign2UMGEditorModule::StartupModule()
{
	FDesign2UMGNotification::RegisterSuccessHandler(
		FDesign2UMGSuccessNotificationHandler::CreateStatic(&ShowEditorSuccessNotification));
	FDesign2UMGNotification::RegisterWarningHandler(
		FDesign2UMGMessageNotificationHandler::CreateStatic(&ShowEditorWarningNotification));
	FDesign2UMGNotification::RegisterErrorHandler(
		FDesign2UMGMessageNotificationHandler::CreateStatic(&ShowEditorErrorNotification));
	FDesign2UMGNotification::RegisterInfoHandler(
		FDesign2UMGMessageNotificationHandler::CreateStatic(&ShowEditorInfoNotification));
	Design2UMGEditorCommandOwner::RegisterConsoleCommands();
	ToolMenusStartupHandle = Design2UMGEditorMenuOwner::RegisterMenus();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	// AddRaw assumes this module object outlives the AssetRegistry delegates: the editor
	// module singleton stays loaded for the whole editor session (unloaded only at engine
	// shutdown or hot reload), and ShutdownModule below removes all four handles again
	// before the module can go away, so the raw pointer never dangles.
	FontAssetAddedHandle = AssetRegistry.OnAssetAdded().AddRaw(this, &FDesign2UMGEditorModule::HandleFontAssetChanged);
	FontAssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddRaw(this, &FDesign2UMGEditorModule::HandleFontAssetChanged);
	FontAssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddRaw(this, &FDesign2UMGEditorModule::HandleFontAssetRenamed);
	FontAssetUpdatedHandle = AssetRegistry.OnAssetUpdated().AddRaw(this, &FDesign2UMGEditorModule::HandleFontAssetChanged);
	Design2UMGEditorMenuOwner::MaybeShowWelcomeDialog();
}

void FDesign2UMGEditorModule::ShutdownModule()
{
	if (FAssetRegistryModule* AssetRegistryModule = FModuleManager::GetModulePtr<FAssetRegistryModule>(TEXT("AssetRegistry")))
	{
		IAssetRegistry& AssetRegistry = AssetRegistryModule->Get();
		AssetRegistry.OnAssetAdded().Remove(FontAssetAddedHandle);
		AssetRegistry.OnAssetRemoved().Remove(FontAssetRemovedHandle);
		AssetRegistry.OnAssetRenamed().Remove(FontAssetRenamedHandle);
		AssetRegistry.OnAssetUpdated().Remove(FontAssetUpdatedHandle);
	}
	FontAssetAddedHandle.Reset();
	FontAssetRemovedHandle.Reset();
	FontAssetRenamedHandle.Reset();
	FontAssetUpdatedHandle.Reset();
	FDesign2UMGNotification::ResetHandlers();
	Design2UMGEditorImportOrchestrator::ShutdownTransientState();
	Design2UMGEditorMenuOwner::UnregisterMenus(ToolMenusStartupHandle, this);
	Design2UMGEditorCommandOwner::UnregisterConsoleCommands();
}

void FDesign2UMGEditorModule::HandleFontAssetChanged(const FAssetData& AssetData)
{
	if (AssetData.AssetClassPath == UFont::StaticClass()->GetClassPathName())
	{
		FFontResolver::ClearCache();
	}
}

void FDesign2UMGEditorModule::HandleFontAssetRenamed(const FAssetData& AssetData, const FString&)
{
	HandleFontAssetChanged(AssetData);
}

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
