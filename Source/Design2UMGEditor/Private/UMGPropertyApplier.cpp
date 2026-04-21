// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "UMGPropertyApplierInternal.h"

void FUMGWidgetUtils::ApplyCommonProperties(UWidget* Widget, const FUMGPropertyBag& Properties)
{
	UMGPropertyApplierInternal::ApplyCommonProperties(Widget, Properties);
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const FUMGPropertyBag& Properties)
{
	UMGPropertyApplierInternal::ApplyWidgetProperties(Widget, Properties);
}

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node)
{
	if (!Widget)
	{
		return;
	}

	UMGPropertyApplierInternal::ApplyWidgetProperties(Widget, Node);

	// Re-apply the few typed-only fields whose runtime fidelity depends on native objects.
	UMGPropertyApplierInternal::ApplyTypedOverrides(Widget, Node);
}
