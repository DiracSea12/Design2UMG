// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "GeneratedAssetLifecycle.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace GeneratedAssetLifecycle
{
	void DiscardUnsavedAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return;
		}

		UPackage* OriginalPackage = Asset->GetOutermost();
		Asset->ClearFlags(RF_Public | RF_Standalone);
		Asset->SetFlags(RF_Transient);
		const FName TransientName = MakeUniqueObjectName(GetTransientPackage(), Asset->GetClass(), Asset->GetFName());
		const bool bMovedToTransientPackage = Asset->Rename(
			*TransientName.ToString(),
			GetTransientPackage(),
			REN_DontCreateRedirectors | REN_NonTransactional | REN_ForceNoResetLoaders);

		if (bMovedToTransientPackage && OriginalPackage && OriginalPackage != GetTransientPackage())
		{
			OriginalPackage->ClearDirtyFlag();
		}
	}

	void RegisterSavedAsset(UObject* Asset)
	{
		if (Asset)
		{
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().AssetCreated(Asset);
			Asset->GetOutermost()->ClearPackageFlags(PKG_NewlyCreated);
		}
	}
}
