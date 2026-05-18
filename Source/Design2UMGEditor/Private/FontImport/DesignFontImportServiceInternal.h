// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchema.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"

class UObject;

namespace DesignFontImportServiceInternal
{
	extern TMap<FString, TMap<FString, FString>> GKnownIconGlyphCache;
	extern TMap<FString, TMap<FString, FString>> GKnownIconNameCache;

	bool IsEngineFallbackFontPath(const FString& AssetPath);
	bool ShouldKeepExistingProjectFont(const FString& ExistingFontPath);
	FString NormalizeIconNameKey(const FString& InName);
	FString ResolveNodeRequestedWeightToken(const TSharedPtr<FDesignNode>& Node);
	void AddRequestedCssWeight(TMap<FString, TSet<int32>>& OutRequestedWeights, const FString& FontFamily, const FString& WeightToken);
	bool DownloadToFile(const FString& Url, const FString& LocalPath);
	bool SaveAssetPackage(UObject* Asset, const FString& PackageName);
	bool HasReloadSafeFontFaceData(const UFontFace* FontFace);
	bool MakeFontFaceReloadSafe(UFontFace* FontFace);
	bool MakeFontFaceReloadSafe(UFontFace* FontFace, TArray<uint8>&& ReplacementData);
	bool TypefaceEntryHasReloadSafeData(const FTypefaceEntry& Entry);
	bool LoadFontBytesFromSource(const FString& SafeName, const FString& Source, TArray<uint8>& OutFontData);
	FString ResolveFont(const FString& FontName, const FString& Url);
}
