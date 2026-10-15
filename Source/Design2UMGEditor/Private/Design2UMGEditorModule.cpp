// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorModule.h"

#include "Design2UMGAssetRegistryAccess.h"
#include "Design2UMGEditorCommandOwner.h"
#include "Design2UMGEditorImportOrchestrator.h"
#include "Design2UMGEditorMenuOwner.h"
#include "Design2UMGFontMetrics.h"
#include "Design2UMGFontAccess.h"
#include "Design2UMGLog.h"
#include "Design2UMGNotifications.h"
#include "Design2UMGSettings.h"
#include "FontResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Misc/CoreDelegates.h"
#include "Misc/EngineVersionComparison.h"
#include "Subsystems/ImportSubsystem.h"
#include "IContentBrowserSingleton.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	FSimpleMulticastDelegate& GetPostEngineInitDelegate()
	{
#if UE_VERSION_OLDER_THAN(5, 8, 0)
		return FCoreDelegates::OnPostEngineInit;
#else
		return FCoreDelegates::GetOnPostEngineInit();
#endif
	}

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
				FAssetData AssetData = Design2UMGAssetRegistryAccess::GetAssetByObjectPath(
					AssetRegistry.Get(), FSoftObjectPath(AssetPath));
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
	// The import subsystem appears after module startup on a cold editor launch;
	// an editor module reload can subscribe immediately.
	if (!TrySubscribeFontReimportHook())
	{
		GetPostEngineInitDelegate().AddRaw(this, &FDesign2UMGEditorModule::HandlePostEngineInit);
	}
	Design2UMGEditorMenuOwner::MaybeShowWelcomeDialog();
}

void FDesign2UMGEditorModule::ShutdownModule()
{
	GetPostEngineInitDelegate().RemoveAll(this);
	if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
	{
		ImportSubsystem->OnAssetReimport.RemoveAll(this);
	}
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
	if (Design2UMGFontAccess::IsFontAsset(AssetData))
	{
		FFontResolver::ClearCache();
	}
}

void FDesign2UMGEditorModule::HandleFontAssetRenamed(const FAssetData& AssetData, const FString&)
{
	HandleFontAssetChanged(AssetData);
}

bool FDesign2UMGEditorModule::TrySubscribeFontReimportHook()
{
	UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr;
	if (!ImportSubsystem)
	{
		return false;
	}
	ImportSubsystem->OnAssetReimport.AddRaw(this, &FDesign2UMGEditorModule::HandleAssetReimported);
	return true;
}

void FDesign2UMGEditorModule::HandlePostEngineInit()
{
	TrySubscribeFontReimportHook();
}

void FDesign2UMGEditorModule::HandleAssetReimported(UObject* ReimportedObject)
{
	// Both composite font and individual face imports can change measured metrics.
	if (ReimportedObject && (ReimportedObject->IsA<UFont>() || ReimportedObject->IsA<UFontFace>()))
	{
		Design2UMGFontMetrics::ClearCache();
	}
}

IMPLEMENT_MODULE(FDesign2UMGEditorModule, Design2UMGEditor)
