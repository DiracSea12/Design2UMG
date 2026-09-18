// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class UObject;
struct FAssetData;

/**
 * Editor module entry point for Design2UMG.
 */
class DESIGN2UMGEDITOR_API FDesign2UMGEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	bool TrySubscribeFontReimportHook();
	void HandlePostEngineInit();
	void HandleAssetReimported(UObject* ReimportedObject);
	void HandleFontAssetChanged(const FAssetData& AssetData);
	void HandleFontAssetRenamed(const FAssetData& AssetData, const FString& OldObjectPath);

	FDelegateHandle ToolMenusStartupHandle;
	FDelegateHandle FontAssetAddedHandle;
	FDelegateHandle FontAssetRemovedHandle;
	FDelegateHandle FontAssetRenamedHandle;
	FDelegateHandle FontAssetUpdatedHandle;
};
