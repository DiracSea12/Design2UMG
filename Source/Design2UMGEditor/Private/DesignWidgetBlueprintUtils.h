// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT
#pragma once

class UWidget;
class UWidgetBlueprint;

namespace DesignWidgetBlueprintUtils
{
	/** Delete a subtree without opening a widget editor. The caller authorizes removing its references. */
	void DeleteSubtreeSilently(UWidgetBlueprint* Blueprint, UWidget* Root);
}
