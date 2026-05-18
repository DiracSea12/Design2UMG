// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "DesignFontImportAutomationPaths.h"

namespace DesignFontImportAutomationPaths
{
	FString NormalizeFontImportPath(const FString& InPath)
	{
		FString Normalized = InPath.TrimStartAndEnd();
		Normalized.ReplaceInline(TEXT("\\"), TEXT("/"));
		while (Normalized.EndsWith(TEXT("/")))
		{
			Normalized.LeftChopInline(1, EAllowShrinking::No);
		}
		return Normalized;
	}

	bool CanReusePinnedFontImportPath(const FString& AutomationFontImportPath)
	{
		const FString NormalizedPath = NormalizeFontImportPath(AutomationFontImportPath);
		return NormalizedPath.IsEmpty()
			|| NormalizedPath.Equals(TEXT("/Game/Design2UMG/Automation/ImportedFonts"), ESearchCase::IgnoreCase);
	}

	bool IsAutomationImportedFontPath(const FString& ExistingFontPath)
	{
		return ExistingFontPath.Contains(TEXT("/Design2UMG/Automation/"), ESearchCase::IgnoreCase)
			&& ExistingFontPath.Contains(TEXT("/ImportedFonts/"), ESearchCase::IgnoreCase);
	}
}
