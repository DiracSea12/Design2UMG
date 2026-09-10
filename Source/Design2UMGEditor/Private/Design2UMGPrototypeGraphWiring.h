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
	 * Destroys every node previously authored by this wiring (identified via the shared node comment),
	 * including whole generated keyboard-override function graphs carrying marker nodes. A function
	 * graph that contains both marker nodes and unmarked nodes is user-modified: it is preserved as a
	 * whole (regeneration for that keyboard event is skipped so the graph can neither be deleted nor
	 * masked), and the graph name is optionally reported through OutPreservedMixedGraphs.
	 * Reimport calls this before applying the update plan: generated wiring is plugin-owned transient
	 * content that the save re-authors, and it must not feed reimport's "referenced by blueprint graphs"
	 * safety gates (those protect genuinely user-authored nodes only).
	 */
	void ClearGeneratedBindings(UWidgetBlueprint* WBP, TArray<FString>* OutPreservedMixedGraphs = nullptr);

	/**
	 * Clears previously generated prototype navigation nodes, then wires eligible bindings:
	 * mouse triggers (Click/Hover/Press) with exactly one step, and keyboard triggers
	 * (ON_KEY_DOWN/ON_KEY_UP) with exactly one Navigate or OpenLink step.
	 *   - OpenLink: bound event -> UKismetSystemLibrary::LaunchURL(url literal).
	 *   - Navigate without transition: bound event -> VariableGet(history) ->
	 *     KismetArrayLibrary::Array_Add(history, UWidgetSwitcher::GetActiveWidgetIndex(switcher)) ->
	 *     VariableGet(switcher) -> UWidgetSwitcher::SetActiveWidgetIndex(resolved page index).
	 *     The history push runs immediately before the switch so the pushed index is the page
	 *     actually left.
	 *   - Navigate with generated transition animations: as above, prefixed by
	 *     PlayAnimation(outgoing) -> Delay(phase duration) and suffixed by PlayAnimation(incoming).
	 *   - Overlay: bound event -> VariableGet(overlay host) -> UWidget::SetVisibility(SelfHitTestInvisible).
	 *     The old runtime reparented the overlay page into the host, which standard nodes cannot
	 *     express; the visible host standing in for the overlay layer is the decided encoding.
	 *   - Back: bound event -> [host SetVisibility(Collapsed) when an overlay host exists: the old
	 *     runtime popped the overlay before history] -> Branch(KismetArrayLibrary::Array_IsEmpty(history))
	 *     with the true path as a no-op; false path -> SetActiveWidgetIndex(Array_LastIndex(history)) ->
	 *     Array_Remove(history, same index).
	 *   - Keyboard Navigate/OpenLink: a generated function-graph override of UUserWidget::OnKeyDown /
	 *     OnKeyUp (they return FEventReply, so they cannot be ubergraph events), Branch per key code
	 *     (UKismetInputLibrary::GetKey(InKeyEvent) == literal FKey) guarding the same step chains;
	 *     the class CDO is flagged focusable so the events can actually fire.
	 * The history stack is the generated TArray&lt;int32&gt; member variable named by
	 * NavHistoryVariableName (created on first need; re-generation reuses it).
	 * All other bindings (multi-step programs, SetVariable/SetVariableMode/conditional flows, drag
	 * triggers, keyboard steps other than Navigate/OpenLink) are skipped with one Verbose log +
	 * OutWarnings entry each.
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
