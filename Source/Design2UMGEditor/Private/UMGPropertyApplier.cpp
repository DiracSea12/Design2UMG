// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetUtils.h"
#include "UMGPropertyApplierInternal.h"

void FUMGWidgetUtils::ApplyWidgetProperties(UWidget* Widget, const FUMGWidgetNode& Node)
{
	if (!Widget)
	{
		return;
	}

	UMGPropertyApplierInternal::ApplyWidgetProperties(Widget, Node);
}
