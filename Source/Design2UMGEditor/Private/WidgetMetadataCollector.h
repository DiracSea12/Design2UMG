// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "Design2UMGFigmaMetadataExtensions.h"
#include "UMGSchema.h"

#include "Components/Widget.h"

namespace FWidgetMetadataCollector
{
	inline void TryAppend(UWidget* Widget, const FUMGWidgetNode& Node, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* Records)
	{
		const FUMGSourceNodeData& SourceNode = Node.Source();
		if (!Widget || !Records || !SourceNode.HasAnyFigmaComponentSemantics())
		{
			return;
		}

		FDesign2UMGFigmaWidgetMetadataRecord Record;
		Record.WidgetName = Widget->GetFName();
		Record.WidgetId = Node.Id;
		Record.SourceNode = SourceNode;
		if (Record.IsValid())
		{
			Records->Add(Record);
		}
	}
}
