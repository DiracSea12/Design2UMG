// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
struct FUMGWidgetNode;
struct FUMGPrototypeNavigationData;
struct FUMGPrototypeTransitionData;

/**
 * Phase 2 prototype transition animations: standard-asset UWidgetAnimation generation for the
 * bindings the graph wiring wires. Only simple mouse-trigger bindings with a single Navigate step
 * and an animated transition get animations; every created object is an engine class
 * (UWidgetAnimation + UMovieScene subobject), so the generated asset stays free of plugin UObjects.
 */
namespace UMGPrototypeAnimationBuilder
{
	/**
	 * Regenerate-safe length of one transition half. The wiring's Delay literal must equal the
	 * created animations' length, so both read this single rule.
	 */
	float ResolvePhaseDurationSeconds(const FUMGPrototypeTransitionData& Transition);

	/**
	 * Removes every previously generated navigation animation (name-prefix match) from the
	 * blueprint, then authors the out/in transition animations for eligible bindings and writes
	 * their names into the navigation IR (Transition.Outgoing/IncomingAnimationName) for the graph
	 * wiring to consume. The animated widget is the prototype switcher, resolved from the built
	 * tree (unique base-name match); the slide offset comes from the IR switcher node bounds.
	 *
	 * Contract: the widget tree must already be built (final switcher name), and the call must run
	 * before SaveWidgetBlueprint's structural mark so the skeleton regen turns the animations into
	 * class properties the wiring reads by name.
	 */
	void GenerateNavigationAnimations(
		UWidgetBlueprint* WidgetBlueprint,
		const FUMGWidgetNode& RootNode,
		FUMGPrototypeNavigationData& NavigationData);
}
