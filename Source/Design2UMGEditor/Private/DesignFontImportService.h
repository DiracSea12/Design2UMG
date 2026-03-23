// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FDesignDocument;

namespace DesignFontImportService
{
	void PrepareDocumentFonts(const FDesignDocument& Document);
	FString EnsureFontFamilyAvailable(const FString& FontFamily);
	void PrimeKnownIconFamily(const FString& FontFamily);
	void ResetTransientState();
}
