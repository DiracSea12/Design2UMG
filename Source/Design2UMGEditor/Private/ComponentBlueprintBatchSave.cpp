// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGGenerator.h"
#include "Design2UMGLog.h"
#include "GeneratedAssetLifecycle.h"
#include "WidgetBlueprint.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"
#include "PackageTools.h"
#include "Editor.h"
#include "Editor/Transactor.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
	struct FOriginalFile
	{
		FString Path;
		TArray<uint8> Bytes;
		bool Existed = false;
	};
}

bool FUMGGenerator::RestoreUnsavedComponentBatch(const TArray<UWidgetBlueprint*>& Assets, FDesign2UMGGenerationOutput* Output)
{
	if (Output && Output->ComponentBatchTransactionId.IsValid())
	{
		const int32 Last = GEditor->Trans->GetQueueLength() - GEditor->Trans->GetUndoCount() - 1;
		const FTransaction* Transaction = Last >= 0 ? GEditor->Trans->GetTransaction(Last) : nullptr;
		if (GEditor->IsTransactionActive() || !Transaction || Transaction->GetId() != Output->ComponentBatchTransactionId)
		{
			Output->MappingDiagnostics.Add({TEXT("component_batch_recovery_incomplete"),
				TEXT("Editor changes followed the batch build; automatic undo was skipped to preserve those changes")});
			return false;
		}
		const bool Undone = GEditor->UndoTransaction(false);
		if (!Undone)
		{
			Output->MappingDiagnostics.Add({TEXT("component_batch_recovery_incomplete"), TEXT("Editor undo did not restore the component batch")});
			return false;
		}
		Output->ComponentBatchTransactionId.Invalidate();
		bool Restored = true;
		for (UWidgetBlueprint* Asset : Assets)
		{
			if (!Asset) continue;
			const FString AssetFile = FPackageName::LongPackageNameToFilename(
				Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
			if (IFileManager::Get().FileExists(*AssetFile))
			{
				FKismetEditorUtilities::CompileBlueprint(Asset);
				if (Asset->Status == BS_Error)
				{
					Restored = false;
					Output->MappingDiagnostics.Add({TEXT("component_batch_recovery_incomplete"), TEXT("Restored blueprint failed to compile: ") + Asset->GetPathName()});
				}
			}
			else GeneratedAssetLifecycle::DiscardUnsavedAsset(Asset);
		}
		Output->Components.Reset();
		return Restored;
	}
	TArray<UPackage*> ExistingPackages;
	for (UWidgetBlueprint* Asset : Assets)
	{
		if (!Asset) continue;
		const FString AssetFile = FPackageName::LongPackageNameToFilename(
			Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
		if (IFileManager::Get().FileExists(*AssetFile))
		{
			// Reload can compile dependent blueprints before replacing their partial trees.
			// Do not retain variable GUIDs from the old tree through that intermediate compile.
			Asset->WidgetVariableNameToGuidMap.Empty();
			ExistingPackages.AddUnique(Asset->GetOutermost());
		}
		else GeneratedAssetLifecycle::DiscardUnsavedAsset(Asset);
	}
	if (Output) Output->Components.Reset();
	if (ExistingPackages.IsEmpty()) return true;
	FText ReloadError;
	const bool Restored = UPackageTools::ReloadPackages(ExistingPackages, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
	if (!Restored && Output) Output->MappingDiagnostics.Add({TEXT("component_reload_failed"), ReloadError.ToString()});
	return Restored;
}

bool FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* Output,
	const FUMGPrototypeNavigationData* Navigation)
{
	if (!WBP || !Output || (!Output->Manifest.bWholeAssetOwned && !Output->Manifest.bComponentBatchRoot))
		return SaveSingleWidgetBlueprint(WBP, Output, Navigation);

	TArray<UWidgetBlueprint*> Assets;
	for (const auto& Component : Output->Components) Assets.Add(Component.Blueprint);
	Assets.Add(WBP);
	TArray<FOriginalFile> Originals;
	TArray<UWidgetBlueprint*> NewAssets;
	for (UWidgetBlueprint* Asset : Assets)
	{
		if (!Asset) return false;
		const FString Package = Asset->GetOutermost()->GetName();
		const FString AssetFile = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().FileExists(*AssetFile)) NewAssets.Add(Asset);
		for (const FString& Path : {AssetFile, Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package)})
		{
			FString WriteError;
			if (!GeneratedAssetLifecycle::IsFileWritableForSave(Path, Path != AssetFile, WriteError))
			{
				Output->MappingDiagnostics.Add({TEXT("component_save_blocked"), WriteError});
				RestoreUnsavedComponentBatch(Assets, Output);
				return false;
			}
			FOriginalFile& Original = Originals.AddDefaulted_GetRef();
			Original.Path = Path;
			Original.Existed = IFileManager::Get().FileExists(*Path);
			if (Original.Existed && !FFileHelper::LoadFileToArray(Original.Bytes, *Path))
			{
				Output->MappingDiagnostics.Add({TEXT("component_backup_failed"), TEXT("Could not preserve original asset file: ") + Path});
				RestoreUnsavedComponentBatch(Assets, Output);
				return false;
			}
		}
	}
	bool Saved = true;
	for (auto& Component : Output->Components)
	{
		if (!SaveSingleWidgetBlueprint(Component.Blueprint, Component.Output.Get(), nullptr))
		{
			Output->MappingDiagnostics.Add({TEXT("component_save_failed"), TEXT("Failed to save dependency: ") + Component.Blueprint->GetPathName()});
			Saved = false;
			break;
		}
	}
	if (Saved) Saved = SaveSingleWidgetBlueprint(WBP, Output, Navigation);
	if (Saved)
	{
		Output->ComponentBatchTransactionId.Invalidate();
		return true;
	}

	bool Restored = true;
	for (const FOriginalFile& Original : Originals)
	{
		const bool FileRestored = Original.Existed
			? FFileHelper::SaveArrayToFile(Original.Bytes, *Original.Path)
			: IFileManager::Get().Delete(*Original.Path, false, true);
		if (!FileRestored)
		{
			Restored = false;
			Output->MappingDiagnostics.Add({TEXT("component_restore_failed"), TEXT("Failed to restore file: ") + Original.Path});
		}
	}
	if (Restored)
	{
		// Reload restored packages together so dependent generated classes are reinstanced consistently.
		Restored = RestoreUnsavedComponentBatch(Assets, Output);
	}
	else
	{
		for (UWidgetBlueprint* NewAsset : NewAssets) GeneratedAssetLifecycle::DiscardUnsavedAsset(NewAsset);
		Output->Components.Reset();
	}
	Output->Components.Reset();
	Output->MappingDiagnostics.Add({Restored ? TEXT("component_batch_restored") : TEXT("component_batch_recovery_incomplete"),
		Restored ? TEXT("Batch save failed; original asset files and manifests restored") : TEXT("Batch save failed; recovery requires attention to the reported files")});
	return false;
}
