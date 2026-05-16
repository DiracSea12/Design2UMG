// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "UMGSchema.h"

class UWidgetBlueprint;

namespace UMGPrototypeAnimationBuilder
{
	void GenerateNavigationAnimations(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGWidgetNode& RootNode,
		FUMGPrototypeNavigationData& NavigationData);

	void ConfigureNavigationExtension(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGPrototypeNavigationData& NavigationData);
}