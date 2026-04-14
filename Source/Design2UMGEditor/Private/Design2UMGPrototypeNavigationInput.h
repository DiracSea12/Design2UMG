// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UDesign2UMGPrototypeNavigationUserWidgetExtension;
struct FKey;

namespace Design2UMGPrototypeNavigationInput
{
	void ResetHistory(UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension);
	void PushHistory(UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension, int32 PageIndex);
	bool TryPopHistory(
		UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension,
		int32 CurrentPageIndex,
		int32& OutPageIndex);

	bool MatchesKeyCode(const FKey& Key, const FString& PrototypeKeyCode);

	void RegisterKeyboardExtension(UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension);
	void UnregisterKeyboardExtension(UDesign2UMGPrototypeNavigationUserWidgetExtension* Extension);
}
