// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

struct FResolvedRichTextRun
{
	FString RowName;
	FString Content;
	FString FontFamily;
	FString TypefaceFontName;
	float FontSize = 0.0f;
	float DesignFontSize = 0.0f;
	float LetterSpacing = 0.0f;
	bool bHasFontSize = false;
	bool bHasLetterSpacing = false;
	bool bUnderline = false;
	bool bStrikethrough = false;
	bool bHasColor = false;
	FString Color;
	FString Href;
};

namespace RichTextStyleRowSerializer
{
	// Serializes resolved runs into a condensed JSON object string.
	// Returns false and leaves OutJson empty if serialization fails.
	bool Serialize(const TArray<FResolvedRichTextRun>& Runs, FString& OutJson);
}
