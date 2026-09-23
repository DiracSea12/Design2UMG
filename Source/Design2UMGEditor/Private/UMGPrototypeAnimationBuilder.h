// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
class UWidgetAnimation;
struct FUMGWidgetNode;
struct FUMGPrototypeNavigationData;
struct FUMGPrototypeTransitionData;

/**
 * Generate engine-standard UWidgetAnimation/UMovieScene assets for animated
 * single-action mouse Navigate bindings. Generated assets need no plugin UObject classes.
 */
namespace UMGPrototypeAnimationBuilder
{
	/** The same ownership rule used when replacing generated navigation animations. */
	bool IsGeneratedNavigationAnimation(const UWidgetAnimation* Animation);
	/**
	 * Regenerate-safe length of one transition half. The wiring's Delay literal must equal the
	 * created animations' length, so both read this single rule.
	 */
	float ResolvePhaseDurationSeconds(const FUMGPrototypeTransitionData& Transition);

	/**
	 * Replace generated transition animations and record their names in navigation IR.
	 * Resolve the switcher from the completed widget tree and its extent from the IR.
	 * Run before SaveWidgetBlueprint refreshes the skeleton used by graph wiring.
	 */
	void GenerateNavigationAnimations(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGWidgetNode& RootNode,
		FUMGPrototypeNavigationData& NavigationData);
}
