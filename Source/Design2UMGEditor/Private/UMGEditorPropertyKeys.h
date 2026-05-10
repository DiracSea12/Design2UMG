// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

namespace UMGPropertyKeys
{
	// Editor-only property bag keys consumed by appliers and editor-private tests.
	// These are intentionally outside Design2UMGCore/Public because current code
	// has no Core typed-property inference, serialization, or resource use.
	inline constexpr const TCHAR* ColumnFill = TEXT("ColumnFill");
	inline constexpr const TCHAR* Options = TEXT("Options");
	inline constexpr const TCHAR* Placement = TEXT("Placement");
	inline constexpr const TCHAR* RowFill = TEXT("RowFill");
	inline constexpr const TCHAR* SelectedOption = TEXT("SelectedOption");
}
