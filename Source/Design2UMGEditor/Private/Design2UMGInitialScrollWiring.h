// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"

class UScrollBox;
class UWidgetBlueprint;

/** UMG has no persisted scroll position, so a source scroll position is restored by generated PreConstruct calls. */
namespace Design2UMGInitialScrollWiring
{
	void SetInitialOffset(UScrollBox* ScrollBox, float Offset);
	/** Rebuilds the generated calls for every scroll box that carries an initial offset. */
	void Wire(UWidgetBlueprint* WBP);
}
