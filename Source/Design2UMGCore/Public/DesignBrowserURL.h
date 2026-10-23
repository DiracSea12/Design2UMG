// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace DesignBrowserURL
{
	/** Anchor a page URI without treating query/fragment or escaped delimiters as file-name characters. */
	DESIGN2UMGCORE_API FString Resolve(const FString& URL, const FString& SourceFilePath = FString());
}
