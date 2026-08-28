// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

/**
 * Automated construction channel for WBP event bindings (C4).
 *
 * FDelegateEditorBinding (WBP->Bindings) can only be created by hand via the UMG details panel in the editor;
 * there is no public entry from the command line / Python, so black-box binding scenarios could not run for real.
 * This adds a minimal channel in the style of the Design2UMG.* command family: enumerate + add (including
 * function-graph creation), directly usable via the Design2UMG.WidgetBindings console command.
 */
namespace Design2UMGWidgetBindingTools
{
	/** Enumerate the WBP's editor-time event bindings; returns a text list of "WidgetName.PropertyName -> FunctionName". */
	TArray<FString> GetEventBindingSummaries(const UWidgetBlueprint* WBP);

	/**
	 * Add an event binding to the given widget (FDelegateEditorBinding, Kind=Function).
	 * - Validates that the widget exists and PropertyName is a delegate property on the widget class (unicast or multicast);
	 * - Creates a same-named empty function graph when the FunctionName does not exist yet (so compile-time
	 *   FDelegateEditorBinding::IsBindingValid resolves the function and the binding is not compiled away);
	 * - Only one function may be bound per widget+property (engine semantics): an existing binding is replaced
	 *   with the new function; adding an identical binding succeeds directly (idempotent).
	 * Memory-only: no compile, no save; persistence is up to the caller (the console path uses
	 * FUMGGenerator::SaveWidgetBlueprint to compile + save). Returns false and fills OutError on failure.
	 */
	bool AddEventBinding(
		UWidgetBlueprint* WBP,
		const FString& WidgetName,
		const FString& PropertyName,
		const FString& FunctionName,
		FString& OutError);

	/** Entry point of the Design2UMG.WidgetBindings console command. */
	void HandleConsoleCommand(const TArray<FString>& Args);
}
