// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "GeneratedAssetLifecycle.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace GeneratedAssetLifecycle
{
	bool IsFileWritableForSave(const FString& InPath, bool bAllowWriteProbe, FString& OutReason)
	{
		if (!IFileManager::Get().FileExists(*InPath))
		{
			return true;
		}
		if (IFileManager::Get().IsReadOnly(*InPath))
		{
			OutReason = FString::Printf(TEXT("'%s' is read-only"), *InPath);
			return false;
		}
		if (!bAllowWriteProbe)
		{
			return true;
		}
		IFileHandle* Probe = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(
			*InPath, /*bAppend=*/true, /*bAllowRead=*/false);
		if (!Probe)
		{
			OutReason = FString::Printf(TEXT("write probe failed on '%s'"), *InPath);
			return false;
		}
		delete Probe;
		return true;
	}

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
