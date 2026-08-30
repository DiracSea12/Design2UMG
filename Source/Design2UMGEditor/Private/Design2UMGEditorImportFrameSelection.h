// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignImportTypes.h"

namespace Design2UMGEditorImportFrameSelection
{
	void ExecuteFrameImport(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		TFunction<void(const FString&)> ImportFrame);
}
