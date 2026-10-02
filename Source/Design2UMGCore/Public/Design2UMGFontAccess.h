// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "Engine/Font.h"
#include "Misc/EngineVersionComparison.h"

namespace Design2UMGFontAccess
{
	inline FCompositeFont& GetMutableCompositeFont(UFont& Font)
	{
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		return Font.CompositeFont;
#else
		return Font.GetMutableInternalCompositeFont();
#endif
	}
}
