// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "UMGWidgetNodeAccess.h"
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

	// Runtime property application always starts from the typed snapshot.
	const FUMGPropertyBag TypedProperties = Design2UMGWidgetNodeAccess::BuildTypedPropertyBag(Node);
	UMGPropertyApplierInternal::ApplyWidgetProperties(Widget, TypedProperties);

	// Re-apply the few typed-only fields whose runtime fidelity depends on native objects.
	UMGPropertyApplierInternal::ApplyTypedOverrides(Widget, Node);
}
