// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#include "UMGGenerator.h"
#include "Misc/EngineVersionComparison.h"
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
		bool bManifest = false;
	};
}

EDesign2UMGMemoryState FUMGGenerator::RestoreUnsavedComponentBulk(const TArray<UWidgetBlueprint*>& Assets, FDesign2UMGGenerationOutput* Output)
{
	if (Output && Output->ComponentBulkTransactionId.IsValid())
	{
		if (!GeneratedAssetLifecycle::UndoOwnedTransaction(Output->ComponentBulkTransactionId))
		{
			Output->MappingDiagnostics.Add({TEXT("component_batch_recovery_incomplete"),
				TEXT("Bulk undo failed or its transaction is no longer current; in-memory recovery is incomplete")});
			return EDesign2UMGMemoryState::RecoveryFailed;
		}
		Output->ComponentBulkTransactionId.Invalidate();
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
			else Restored &= GeneratedAssetLifecycle::DiscardUnsavedAsset(Asset);
		}
		Output->Components.Reset();
		return Restored ? EDesign2UMGMemoryState::Restored : EDesign2UMGMemoryState::RecoveryFailed;
	}
	bool Restored = true;
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
#if !UE_VERSION_OLDER_THAN(5, 6, 0)
			Asset->WidgetVariableNameToGuidMap.Empty();
#endif
			ExistingPackages.AddUnique(Asset->GetOutermost());
		}
		else Restored &= GeneratedAssetLifecycle::DiscardUnsavedAsset(Asset);
	}
	if (Output) Output->Components.Reset();
	if (ExistingPackages.IsEmpty()) return !Restored ? EDesign2UMGMemoryState::RecoveryFailed
		: (Assets.IsEmpty() ? EDesign2UMGMemoryState::Unchanged : EDesign2UMGMemoryState::Restored);
	FText ReloadError;
	const bool Reloaded = UPackageTools::ReloadPackages(ExistingPackages, ReloadError, EReloadPackagesInteractionMode::AssumePositive);
	if (!Reloaded && Output) Output->MappingDiagnostics.Add({TEXT("component_reload_failed"), ReloadError.ToString()});
	Restored &= Reloaded;
	return Restored ? EDesign2UMGMemoryState::Restored : EDesign2UMGMemoryState::RecoveryFailed;
}

FDesign2UMGCommitResult FUMGGenerator::SaveWidgetBlueprint(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* Output,
	const FUMGPrototypeNavigationData* Navigation)
{
	FDesign2UMGCommitResult Result = WBP && Output && (Output->Manifest.bWholeAssetOwned || Output->Manifest.bComponentBulkRoot)
		? SaveComponentBlueprintBulk(WBP, Output, Navigation) : SaveSingleWidgetBlueprint(WBP, Output, Navigation);
	if (Output) Output->Commit = Result;
	return Result;
}

FDesign2UMGCommitResult FUMGGenerator::SaveComponentBlueprintBulk(UWidgetBlueprint* WBP, FDesign2UMGGenerationOutput* Output,
	const FUMGPrototypeNavigationData* Navigation)
{
	FDesign2UMGCommitResult Result;
	Result.AssetPath = WBP->GetPathName();
	Result.Memory = EDesign2UMGMemoryState::Modified;
	TArray<UWidgetBlueprint*> Assets;
	for (const auto& Component : Output->Components) Assets.Add(Component.Blueprint);
	Assets.Add(WBP);
	const auto RestoreMemory = [&]()
	{
		Result.Memory = RestoreUnsavedComponentBulk(Assets, Output);
		Result.Status = Result.Memory == EDesign2UMGMemoryState::RecoveryFailed
			? EDesign2UMGCommitStatus::RollbackIncomplete : EDesign2UMGCommitStatus::NotCommitted;
	};
	TArray<FOriginalFile> Originals;
	TArray<UWidgetBlueprint*> NewAssets;
	for (UWidgetBlueprint* Asset : Assets)
	{
		if (!Asset)
		{
			RestoreMemory();
			return Result;
		}
		const FString Package = Asset->GetOutermost()->GetName();
		const FString AssetFile = FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension());
		if (!IFileManager::Get().FileExists(*AssetFile)) NewAssets.Add(Asset);
		for (const FString& Path : {AssetFile, Design2UMGGenerationManifest::BuildSidecarPathForAsset(Package)})
		{
			FString WriteError;
			if (!GeneratedAssetLifecycle::IsFileWritableForSave(Path, Path != AssetFile, WriteError))
			{
				Output->MappingDiagnostics.Add({TEXT("component_save_blocked"), WriteError});
				RestoreMemory();
				return Result;
			}
			FOriginalFile& Original = Originals.AddDefaulted_GetRef();
			Original.Path = Path;
			Original.bManifest = Path != AssetFile;
			Original.Existed = IFileManager::Get().FileExists(*Path);
			if (Original.Existed && !FFileHelper::LoadFileToArray(Original.Bytes, *Path))
			{
				Output->MappingDiagnostics.Add({TEXT("component_backup_failed"), TEXT("Could not preserve original asset file: ") + Path});
				RestoreMemory();
				return Result;
			}
		}
	}
	bool Saved = true;
	bool Degraded = false;
	for (auto& Component : Output->Components)
	{
		const FDesign2UMGCommitResult SavedComponent = SaveSingleWidgetBlueprint(Component.Blueprint, Component.Output.Get(), nullptr);
		Component.Output->Commit = SavedComponent;
		Degraded |= SavedComponent.Status == EDesign2UMGCommitStatus::SucceededWithDegradation;
		if (!SavedComponent.IsSuccess())
		{
			Output->MappingDiagnostics.Add({TEXT("component_save_failed"), TEXT("Failed to save dependency: ") + SavedComponent.AssetPath});
			Saved = false;
			break;
		}
	}
	if (Saved)
	{
		Result = SaveSingleWidgetBlueprint(WBP, Output, Navigation);
		Saved = Result.IsSuccess();
	}
	if (Saved)
	{
		if (Degraded) Result.Status = EDesign2UMGCommitStatus::SucceededWithDegradation;
		Output->ComponentBulkTransactionId.Invalidate();
		return Result;
	}

	// Disk recovery and memory recovery are separate outcomes.
	Result.Asset = EDesign2UMGFileState::Restored;
	Result.Manifest = EDesign2UMGFileState::Restored;
	for (const FOriginalFile& Original : Originals)
	{
		const bool FileRestored = Original.Existed
			? FFileHelper::SaveArrayToFile(Original.Bytes, *Original.Path)
			: IFileManager::Get().Delete(*Original.Path, false, true);
		if (!FileRestored)
		{
			(Original.bManifest ? Result.Manifest : Result.Asset) = EDesign2UMGFileState::Indeterminate;
			Output->MappingDiagnostics.Add({TEXT("component_restore_failed"), TEXT("Failed to restore file: ") + Original.Path});
		}
	}
	if (Result.Asset == EDesign2UMGFileState::Restored && Result.Manifest == EDesign2UMGFileState::Restored)
	{
		RestoreMemory();
	}
	else
	{
		Result.Status = EDesign2UMGCommitStatus::RollbackIncomplete;
		bool bNewAssetsDiscarded = true;
		for (UWidgetBlueprint* NewAsset : NewAssets) bNewAssetsDiscarded &= GeneratedAssetLifecycle::DiscardUnsavedAsset(NewAsset);
		if (!bNewAssetsDiscarded) Result.Memory = EDesign2UMGMemoryState::RecoveryFailed;
		else if (NewAssets.Num() == Assets.Num()) Result.Memory = EDesign2UMGMemoryState::Restored;
	}
	Output->Components.Reset();
	const bool Restored = Result.Status != EDesign2UMGCommitStatus::RollbackIncomplete;
	Output->MappingDiagnostics.Add({Restored ? TEXT("component_batch_restored") : TEXT("component_batch_recovery_incomplete"),
		Restored ? TEXT("Bulk save failed; original asset files and manifests restored") : TEXT("Bulk save failed; recovery requires attention to the reported files")});
	return Result;
}
