// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

struct FSlateFontInfo;

/** Asset-backed and file-backed font metrics. Call on the game thread. */
namespace Design2UMGFontMetrics
{
	/** Read hhea lineGap from the selected face, in em units. */
	DESIGN2UMGCORE_API bool TryReadLineGapEm(const FSlateFontInfo& FontInfo, float& OutLineGapEm);

	/** Invalidate cached metrics after font reimport or at Core module shutdown. */
	DESIGN2UMGCORE_API void ClearCache();
}
