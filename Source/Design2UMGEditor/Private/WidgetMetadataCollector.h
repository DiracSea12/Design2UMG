// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGFigmaMetadataExtensions.h"
#include "UMGSchema.h"

class UWidget;

namespace FWidgetMetadataCollector
{
	void TryAppend(UWidget* Widget, const FUMGWidgetNode& Node, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* Records);
}
