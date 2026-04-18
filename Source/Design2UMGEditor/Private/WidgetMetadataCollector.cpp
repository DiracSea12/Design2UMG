// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "WidgetMetadataCollector.h"
#include "Components/Widget.h"

void FWidgetMetadataCollector::TryAppend(UWidget* Widget, const FUMGWidgetNode& Node, TArray<FDesign2UMGFigmaWidgetMetadataRecord>* Records)
{
	if (!Widget || !Records || !Node.Source.HasAnyComponentSemantics())
	{
		return;
	}

	FDesign2UMGFigmaWidgetMetadataRecord Record;
	Record.WidgetName = Widget->GetFName();
	Record.WidgetId = Node.Id;
	Record.SourceNode = Node.Source;
	if (Record.IsValid())
	{
		Records->Add(Record);
	}
}
