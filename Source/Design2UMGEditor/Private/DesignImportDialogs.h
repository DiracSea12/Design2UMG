// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "SDesign2UMGPreviewWindow.h"

struct FDesignFrameInfo;
struct FUMGWidgetData;

namespace DesignImportDialogs
{
	using FFrameSelectedCallback = TFunction<void(const FString& FrameId)>;

	bool OpenPreviewWindow(
		const FUMGWidgetData& Data,
		const FString& OutputPath,
		const FText& WindowTitle,
		FOnPreviewConfirmed OnConfirmed,
		FOnPreviewCancelled OnCancelled = FOnPreviewCancelled());

	void ShowFrameSelector(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		FFrameSelectedCallback OnFrameSelected);
}
