// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGEditorImportFrameSelection.h"

#include "DesignImportDialogs.h"

namespace Design2UMGEditorImportFrameSelection
{
	void ExecuteFrameImport(
		const TArray<FDesignFrameInfo>& Frames,
		const FText& WindowTitle,
		const FText& HintText,
		TFunction<void(const FString&)> ImportFrame)
	{
		if (Frames.Num() == 1)
		{
			ImportFrame(FString());
			return;
		}

		DesignImportDialogs::ShowFrameSelector(
			Frames,
			WindowTitle,
			HintText,
			[ImportFrame = MoveTemp(ImportFrame)](const FString& FrameId) mutable
			{
				ImportFrame(FrameId);
			});
	}
}
