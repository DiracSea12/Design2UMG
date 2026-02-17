// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. PDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FPenDocument;
struct FPenNode;

struct DESIGN2UMGEDITOR_API FFontResolver
{
	/** Scan document for all used font families, compare with project assets, return missing list */
	static TArray<FString> FindMissingFonts(const FPenDocument& Document);

private:
	static void CollectFontFamilies(const TArray<TSharedPtr<FPenNode>>& Nodes, TSet<FString>& OutFamilies);
	static TSet<FString> ScanProjectFontAssets();
};
