// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;
struct FUMGPrototypeNavigationBinding;
struct FUMGPrototypeNavigationData;

/**
 * Phase 1 prototype navigation graph wiring: authors engine-standard Blueprint nodes
 * (UK2Node_ComponentBoundEvent -> UK2Node_VariableGet -> UK2Node_CallFunction) into the
 * WBP ubergraph for simple single-action bindings. Only engine node classes are created;
 * the generated asset stays free of plugin UObjects.
 */
namespace Design2UMGPrototypeGraphWiring
{
	/** Node comment stamped on every generated node; doubles as the marker for idempotent re-wiring. */
	extern const TCHAR NodeMarkerComment[];

	/**
	 * Phase 1 eligibility: mouse trigger (Click/Hover/Press) plus exactly one simple Navigate or
	 * OpenLink step. Shared with the transition animation builder so only bindings the wiring
	 * actually wires get animations authored for them.
	 */
	bool IsSimpleMouseActionBinding(const FUMGPrototypeNavigationBinding& Binding);

	/**
	 * Destroys every node previously authored by this wiring (identified via the shared node comment).
	 * Reimport calls this before applying the update plan: generated wiring is plugin-owned transient
	 * content that the save re-authors, and it must not feed reimport's "referenced by blueprint graphs"
	 * safety gates (those protect genuinely user-authored nodes only).
	 */
	void ClearGeneratedBindings(UWidgetBlueprint* WBP);

	/**
	 * Clears previously generated prototype navigation nodes, then wires Phase 1 eligible bindings:
	 * Click/Hover/Press on a wrapped UButton with exactly one Navigate or OpenLink step.
	 *   - OpenLink: bound event -> UKismetSystemLibrary::LaunchURL(url literal).
	 *   - Navigate without transition: bound event -> VariableGet(switcher) -> UWidgetSwitcher::SetActiveWidgetIndex(resolved page index).
	 *   - Navigate with generated transition animations (the animation builder filled the transition's
	 *     Outgoing/IncomingAnimationName): bound event -> VariableGet(outgoing animation) ->
	 *     UUserWidget::PlayAnimation -> UKismetSystemLibrary::Delay(phase duration literal) ->
	 *     VariableGet(switcher) -> SetActiveWidgetIndex -> VariableGet(incoming animation) -> PlayAnimation.
	 * All other bindings (steps programs, Overlay/Back/SetVariable/SetVariableMode, drag/key triggers)
	 * are skipped with one Verbose log + OutWarnings entry each.
	 *
	 * Contract: must run after the widget tree is built and the skeleton class carries the tree
	 * widgets' properties (i.e. after MarkBlueprintAsStructurallyModified), and before the final
	 * full compile, so nodes reference real properties and the compiler registers the delegate
	 * binding (UComponentDelegateBinding) from the authored source nodes.
	 */
	void WireSimplePrototypeBindings(
		UWidgetBlueprint* WBP,
		const FUMGPrototypeNavigationData& NavigationData,
		TArray<FString>& OutWarnings);
}
