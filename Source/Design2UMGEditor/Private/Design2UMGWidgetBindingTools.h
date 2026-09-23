// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"

class UWidgetBlueprint;

/**
 * Inspect and author WBP event bindings through Design2UMG.WidgetBindings.
 */
namespace Design2UMGWidgetBindingTools
{
	/** Enumerate the WBP's editor-time event bindings; returns a text list of "WidgetName.PropertyName -> FunctionName". */
	TArray<FString> GetEventBindingSummaries(const UWidgetBlueprint* WBP);

	/**
	 * Bind a widget delegate to a function, creating an empty function graph if needed.
	 * Replace a different binding for the same widget/property; identical bindings are a no-op.
	 * Changes are memory-only. The caller compiles and saves. Failure fills OutError.
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
