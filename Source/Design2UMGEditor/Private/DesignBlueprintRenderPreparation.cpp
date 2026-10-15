// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignBlueprintRenderPreparation.h"
#include "Design2UMGLog.h"
#include "DesignPathRasterizer.h"
#include "FontResolver.h"
#include "ProceduralShapeGenerator.h"

#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontCache.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "RenderingThread.h"
#include "Rendering/SlateRenderer.h"
#include "TextureCompiler.h"
#include "UObject/Linker.h"
#include "UObject/Package.h"
#include "UObject/PackageReload.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"

namespace
{
	FString NormalizePackagePath(const FString& PackagePath)
	{
		FString Normalized = PackagePath.TrimStartAndEnd();
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Normalized.EndsWith(TEXT("/")))
		{
			Normalized = Normalized.LeftChop(1);
		}
		return Normalized;
	}

	FString NormalizeWidgetBlueprintObjectPath(const FString& AssetPath)
	{
		FString Normalized = NormalizePackagePath(AssetPath);
		if (Normalized.IsEmpty())
		{
			return Normalized;
		}

		const FString AssetName = FPaths::GetBaseFilename(Normalized);
		const FString ObjectSuffix = TEXT(".") + AssetName;
		if (!AssetName.IsEmpty() && !Normalized.EndsWith(ObjectSuffix))
		{
			Normalized += ObjectSuffix;
		}
		return Normalized;
	}

	bool RefreshPackagesInDirectory(
		const FString& PackagePath,
		const TCHAR* Description,
		const bool bRefreshTextures,
		FString& OutError)
	{
		const FString NormalizedPath = NormalizePackagePath(PackagePath);
		if (NormalizedPath.IsEmpty())
		{
			return true;
		}

		const FString PackageDirectory = FPackageName::LongPackageNameToFilename(NormalizedPath, TEXT(""));
		if (PackageDirectory.IsEmpty() || !IFileManager::Get().DirectoryExists(*PackageDirectory))
		{
			return true;
		}

		TArray<FString> AssetFiles;
		IFileManager::Get().FindFiles(AssetFiles, *(PackageDirectory / TEXT("*.uasset")), true, false);
		FlushAsyncLoading();
		for (const FString& AssetFile : AssetFiles)
		{
			const FString AssetName = FPaths::GetBaseFilename(AssetFile);
			if (AssetName.IsEmpty())
			{
				continue;
			}

			const FString PackageName = NormalizePackagePath(NormalizedPath / AssetName);
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			}
			else if (!Package->IsDirty())
			{
				ResetLoaders(static_cast<UObject*>(Package));
				Package = ReloadPackage(Package, LOAD_None);
				if (!Package)
				{
					OutError = FString::Printf(
						TEXT("Failed to reload %s package before render: %s"), Description, *PackageName);
					return false;
				}
			}

			if (!Package)
			{
				OutError = FString::Printf(
					TEXT("Failed to load %s package before render: %s"), Description, *PackageName);
				return false;
			}

			if (bRefreshTextures)
			{
				UTexture2D* Texture = FindObject<UTexture2D>(Package, *AssetName);
				if (!Texture)
				{
					Texture = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName));
				}
				if (Texture)
				{
					Texture->ConditionalPostLoad();
#if WITH_EDITOR
					FTextureCompilingManager::Get().FinishCompilation({ Texture });
#endif
					Texture->UpdateResource();
				}
			}
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		return true;
	}

	bool RefreshGeneratedTexturePackages(const FString& WidgetBlueprintObjectPath, FString& OutError)
	{
		const FString PackageName = FPackageName::ObjectPathToPackageName(WidgetBlueprintObjectPath);
		if (PackageName.IsEmpty())
		{
			return true;
		}

		const FString PackagePath = FPaths::GetPath(PackageName);
		return RefreshPackagesInDirectory(PackagePath / TEXT("D2U_PathTextures"), TEXT("path texture"), true, OutError)
			&& RefreshPackagesInDirectory(PackagePath / TEXT("D2U_ProceduralTextures"), TEXT("procedural texture"), true, OutError)
			&& RefreshPackagesInDirectory(PackagePath / TEXT("D2U_ImageTransforms"), TEXT("image transform"), true, OutError);
	}

	bool PrepareRenderResources(
		const FString& WidgetBlueprintObjectPath,
		const FBlueprintRenderPreparationOptions& Preparation,
		FString& OutError)
	{
		if (!Preparation.ImportedFontPackagePath.IsEmpty()
			&& !RefreshPackagesInDirectory(
				Preparation.ImportedFontPackagePath,
				TEXT("imported font"),
				false,
				OutError))
		{
			return false;
		}

		return !Preparation.bRefreshGeneratedTextures
			|| RefreshGeneratedTexturePackages(WidgetBlueprintObjectPath, OutError);
	}
}

namespace DesignBlueprintRenderPreparation
{
	void ResetTransientRenderState()
	{
		FlushRenderingCommands();
		FlushAsyncLoading();
		if (FSlateApplication::IsInitialized())
		{
			if (FSlateRenderer* SlateRenderer = FSlateApplication::Get().GetRenderer())
			{
				SlateRenderer->FlushFontCache(TEXT("Design2UMG render preparation"));
				SlateRenderer->GetFontCache()->ConditionalFlushCache();
			}
		}
		FFontResolver::ClearCache();
		FDesignPathRasterizer::ClearCache();
		FProceduralShapeGenerator::ReleaseTransientTextures();
		CollectGarbage(RF_NoFlags, true);
	}

	UWidgetBlueprint* ReloadWidgetBlueprintForRender(
		UWidgetBlueprint* WidgetBlueprint,
		const FBlueprintRenderPreparationOptions* Preparation,
		FString* OutPreparationError)
	{
		if (!WidgetBlueprint)
		{
			return nullptr;
		}

		const FString ObjectPath = NormalizeWidgetBlueprintObjectPath(WidgetBlueprint->GetPathName());
		const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
		if (PackageName.IsEmpty())
		{
			return WidgetBlueprint;
		}

		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			if (OutPreparationError)
			{
				*OutPreparationError = FString::Printf(
					TEXT("Failed to load Widget Blueprint package before render: %s"), *PackageName);
			}
			return nullptr;
		}

		// Preparation may collect garbage or replace resource packages. Keep unsaved
		// blueprint edits alive without preventing clean blueprint package reloads.
		const TStrongObjectPtr<UWidgetBlueprint> DirtyBlueprint(Package->IsDirty() ? WidgetBlueprint : nullptr);
		if (DirtyBlueprint.IsValid())
		{
			UE_LOG(LogDesign2UMG, Warning,
				TEXT("DesignBlueprintRenderPreparation: %s has unsaved changes; skipping pre-render reload and rendering the in-memory version"),
				*PackageName);
		}

		// Template Slate resources belong to this blueprint, not to garbage collection.
		if (WidgetBlueprint->WidgetTree)
		{
			WidgetBlueprint->WidgetTree->ForEachWidget([](UWidget* Widget) { Widget->ReleaseSlateResources(true); });
		}
		const bool bResetTransientState = Preparation && Preparation->bResetTransientRenderState;
		if (bResetTransientState)
		{
			ResetTransientRenderState();
		}

		FString LocalError;
		if (Preparation && !PrepareRenderResources(ObjectPath, *Preparation, LocalError))
		{
			if (bResetTransientState)
			{
				ResetTransientRenderState();
			}
			if (OutPreparationError)
			{
				*OutPreparationError = LocalError;
			}
			return nullptr;
		}

		if (DirtyBlueprint.IsValid())
		{
			if (bResetTransientState)
			{
				ResetTransientRenderState();
			}
			return DirtyBlueprint.Get();
		}

		Package = FindPackage(nullptr, *PackageName);
		if (!Package)
		{
			Package = LoadPackage(nullptr, *PackageName, LOAD_None);
		}
		if (!Package)
		{
			if (bResetTransientState)
			{
				ResetTransientRenderState();
			}
			if (OutPreparationError)
			{
				*OutPreparationError = FString::Printf(
					TEXT("Widget Blueprint package was unavailable after render preparation: %s"), *PackageName);
			}
			return nullptr;
		}

		FlushAsyncLoading();
		ResetLoaders(static_cast<UObject*>(Package));
		if (!ReloadPackage(Package, LOAD_None))
		{
			if (bResetTransientState)
			{
				ResetTransientRenderState();
			}
			if (OutPreparationError)
			{
				*OutPreparationError = FString::Printf(
					TEXT("Failed to reload Widget Blueprint package before render: %s"), *PackageName);
			}
			return nullptr;
		}

		FlushRenderingCommands();
		CollectGarbage(RF_NoFlags, true);
		if (bResetTransientState)
		{
			ResetTransientRenderState();
		}
		if (UWidgetBlueprint* ReloadedBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath))
		{
			return ReloadedBlueprint;
		}

		if (OutPreparationError)
		{
			*OutPreparationError = FString::Printf(
				TEXT("Failed to load Widget Blueprint after package reload: %s"), *ObjectPath);
		}
		return nullptr;
	}

	UWidgetBlueprint* PrepareWidgetBlueprintForRender(
		const FString& WidgetBlueprintAssetPath,
		const FBlueprintRenderPreparationOptions& Preparation,
		const bool bReloadPackage,
		FString* OutPreparationError)
	{
		const FString ObjectPath = NormalizeWidgetBlueprintObjectPath(WidgetBlueprintAssetPath);
		if (bReloadPackage)
		{
			UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
			if (!WidgetBlueprint)
			{
				if (OutPreparationError)
				{
					*OutPreparationError = FString::Printf(
						TEXT("Failed to load Widget Blueprint before render: %s"), *ObjectPath);
				}
				return nullptr;
			}
			return ReloadWidgetBlueprintForRender(WidgetBlueprint, &Preparation, OutPreparationError);
		}

		if (Preparation.bResetTransientRenderState)
		{
			ResetTransientRenderState();
		}

		FString LocalError;
		const bool bPrepared = PrepareRenderResources(ObjectPath, Preparation, LocalError);
		if (Preparation.bResetTransientRenderState)
		{
			ResetTransientRenderState();
		}
		if (!bPrepared)
		{
			if (OutPreparationError)
			{
				*OutPreparationError = LocalError;
			}
			return nullptr;
		}

		UWidgetBlueprint* WidgetBlueprint = LoadObject<UWidgetBlueprint>(nullptr, *ObjectPath);
		if (!WidgetBlueprint && OutPreparationError)
		{
			*OutPreparationError = FString::Printf(
				TEXT("Failed to load Widget Blueprint after render preparation: %s"), *ObjectPath);
		}
		return WidgetBlueprint;
	}
}
