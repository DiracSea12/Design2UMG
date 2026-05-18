// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignFontImportCatalog
{
	FString NormalizeFontFamilyKey(const FString& InFamily);

	const TMap<FString, FString>& GetKnownWebFontUrls();
	const TMap<FString, FString>& GetKnownIconFontCssUrls();
	const TSet<FString>& GetKnownCssFontFamilies();

	int32 NormalizeCssWeightValue(int32 Weight);
	TArray<int32> ResolveKnownCssFontWeights(
		const FString& FontFamily,
		const TMap<FString, TSet<int32>>& RequestedWeightsByFamily);

	FString WeightNameFromNumeric(int32 Weight);
}
