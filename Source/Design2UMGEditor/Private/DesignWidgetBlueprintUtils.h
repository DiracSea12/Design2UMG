// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

class UWidget;
class UWidgetBlueprint;

namespace DesignWidgetBlueprintUtils
{
	/** Register missing widget GUIDs using the compiler's deterministic rule; preserve existing identities. */
	void EnsureWidgetVariableGuids(UWidgetBlueprint* WBP);

	/** Delete a subtree without opening a widget editor. The caller authorizes removing its references. */
	void DeleteSubtreeSilently(UWidgetBlueprint* Blueprint, UWidget* Root);
}
