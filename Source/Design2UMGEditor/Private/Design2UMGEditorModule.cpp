// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"

#include "Design2UMGEditorCommandOwner.h"
#include "Design2UMGEditorDesignSourceRegistry.h"
#include "Design2UMGEditorImportOrchestrator.h"
#include "Design2UMGEditorMenuOwner.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignFontImportService.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
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
	FFontResolver::RegisterMissingFontResolver(
		[](const FString& FontFamily)
		{
			return DesignFontImportService::EnsureFontFamilyAvailable(FontFamily);
		});
	FFontResolver::RegisterKnownIconGlyphPrimer(
		[](const FString& FontFamily)
		{
			DesignFontImportService::PrimeKnownIconFamily(FontFamily);
		});
	Design2UMGEditorDesignSourceRegistry::RegisterBuiltInSources();
	Design2UMGEditorCommandOwner::RegisterConsoleCommands();
	ToolMenusStartupHandle = Design2UMGEditorMenuOwner::RegisterMenus();
	Design2UMGEditorMenuOwner::MaybeShowWelcomeDialog();
}

void FDesign2UMGEditorModule::ShutdownModule()
{
	FDesign2UMGNotification::ResetHandlers();
	FFontResolver::ResetExternalResolvers();
	DesignFontImportService::ResetTransientState();
	Design2UMGEditorImportOrchestrator::ShutdownTransientState();
	Design2UMGEditorMenuOwner::UnregisterMenus(ToolMenusStartupHandle, this);
	Design2UMGEditorCommandOwner::UnregisterConsoleCommands();
	Design2UMGEditorDesignSourceRegistry::UnregisterBuiltInSources();
}

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
