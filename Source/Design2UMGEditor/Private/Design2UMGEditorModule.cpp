// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"

#include "Design2UMGEditorActions.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotification.h"
#include "DesignFontImportService.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
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

		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(SNotificationItem::CS_Success);
		}
	}
}

void FDesign2UMGEditorModule::StartupModule()
{
	FDesign2UMGNotification::RegisterSuccessHandler(
		FDesign2UMGSuccessNotificationHandler::CreateStatic(&ShowEditorSuccessNotification));
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
	Design2UMGEditorActions::RegisterBuiltInDesignSources();
	Design2UMGEditorActions::RegisterConsoleCommands();
	ToolMenusStartupHandle = Design2UMGEditorActions::RegisterMenus();
	Design2UMGEditorActions::MaybeShowWelcomeDialog();
}

void FDesign2UMGEditorModule::ShutdownModule()
{
	FDesign2UMGNotification::ResetSuccessHandler();
	FFontResolver::ResetExternalResolvers();
	DesignFontImportService::ResetTransientState();
	Design2UMGEditorActions::ShutdownTransientState();
	Design2UMGEditorActions::UnregisterMenus(ToolMenusStartupHandle, this);
	Design2UMGEditorActions::UnregisterConsoleCommands();
	Design2UMGEditorActions::UnregisterBuiltInDesignSources();
}

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
