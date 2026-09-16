// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

struct FResolvedRichTextRun
{
	FString RowName;
	FString Content;
	FString FontFamily;
	FString TypefaceFontName;
	TOptional<float> FontSize;
	TOptional<float> LetterSpacing;
	bool bUnderline = false;
	bool bStrikethrough = false;
	TOptional<FString> Color;
	FString Href;
};

namespace RichTextStyleRowSerializer
{
	// Serializes resolved runs into a condensed JSON object string.
	// Returns false and leaves OutJson empty if serialization fails.
	bool Serialize(const TArray<FResolvedRichTextRun>& Runs, FString& OutJson);
}
