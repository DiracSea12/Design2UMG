// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "GeneratedAssetLifecycle.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include <atomic>
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
#if WITH_DEV_AUTOMATION_TESTS
	std::atomic<bool> bAutomationUndoFailureArmed{false};
#endif
}

namespace GeneratedAssetLifecycle
{
	bool UndoOwnedTransaction(const FGuid& TransactionId)
	{
		if (!GEditor || !GEditor->Trans || !TransactionId.IsValid() || GEditor->IsTransactionActive()) return false;
		const int32 Last = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount() - 1;
		const FTransaction* Transaction = Last >= 0 ? GEditor->Trans->GetTransaction(Last) : nullptr;
		if (!Transaction || Transaction->GetId() != TransactionId) return false;
#if WITH_DEV_AUTOMATION_TESTS
		if (bAutomationUndoFailureArmed.exchange(false)) return false;
#endif
		return GEditor->UndoTransaction(false);
	}

#if WITH_DEV_AUTOMATION_TESTS
	void ArmAutomationUndoFailureOnce()
	{
		bAutomationUndoFailureArmed.store(true);
	}
#endif

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

	bool DiscardUnsavedAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return true;
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
		return bMovedToTransientPackage;
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
