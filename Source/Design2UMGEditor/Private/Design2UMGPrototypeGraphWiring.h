// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
struct FUMGPrototypeNavigationBinding;
struct FUMGPrototypeNavigationData;

/**
 * Prototype navigation graph wiring: authors engine-standard Blueprint nodes
 * (UK2Node_ComponentBoundEvent -> UK2Node_VariableGet -> UK2Node_CallFunction) into the
 * WBP ubergraph for simple single-action bindings. Only engine node classes are created;
 * the generated asset stays free of plugin UObjects.
 */
namespace Design2UMGPrototypeGraphWiring
{
	/** Node comment stamped on every generated node; doubles as the marker for idempotent re-wiring. */
	extern const TCHAR NodeMarkerComment[];

	/**
	 * Name of the generated TArray&lt;int32&gt; member variable holding the navigation history
	 * stack: every wired Navigate pushes the current switcher index, Back pops it.
	 */
	extern const TCHAR NavHistoryVariableName[];

	/**
	 * Eligibility: mouse trigger (Click/Hover/Press) plus exactly one simple step among Navigate,
	 * OpenLink, Overlay and Back. Shared with the transition animation builder so only bindings the
	 * wiring actually wires get animations authored for them (the builder further restricts to
	 * Navigate-only transitions internally).
	 */
	bool IsSimpleMouseActionBinding(const FUMGPrototypeNavigationBinding& Binding);

	/**
	 * Remove generated navigation nodes and wholly generated keyboard graphs.
	 * Preserve mixed user/generated graphs and report their names so regeneration skips them.
	 * Reimport clears generated wiring before checking user references to widgets.
	 */
	void ClearGeneratedBindings(UWidgetBlueprint* WBP, TArray<FString>* OutPreservedMixedGraphs = nullptr);

	/**
	 * Regenerate engine-standard graphs for single-action mouse bindings and keyboard
	 * Navigate/OpenLink bindings. Navigate records history and plays available transitions;
 * Overlay controls hosted-page visibility, Back closes an open overlay or pops history,
	 * and OpenLink launches its URL. Keyboard graphs guard by key and active page.
	 * Unsupported bindings produce warnings. User-authored bindings remain intact.
	 * Refresh the Blueprint skeleton before wiring; compile after wiring.
	 */
	void WireSimplePrototypeBindings(
		UWidgetBlueprint* WBP,
		const FUMGPrototypeNavigationData& NavigationData,
		TArray<FString>& OutWarnings);
}
