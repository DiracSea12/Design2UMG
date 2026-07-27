// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"
#include "TextMeasurement.h"

#if WITH_AUTOMATION_WORKER

struct FTextResolutionTrace
{
	FString NodeName;
	float SourceFontSize = 0.0f;
	float CompiledFontSize = 0.0f;
	FVector2D CompiledBounds = FVector2D::ZeroVector;
};

namespace TextMeasurementTestTrace
{
	inline FTextResolutionTrace DumpTextResolutionTrace(const TSharedPtr<FDesignNode>& Node)
	{
		FTextResolutionTrace Trace;
		if (!Node.IsValid())
		{
			return Trace;
		}

		Trace.NodeName = Node->Name;
		Trace.SourceFontSize = Node->Text.FontSize;
		Trace.CompiledFontSize = TextMeasurement::ResolveTextFontSizeForUMG(Node);
		Trace.CompiledBounds = TextMeasurement::MeasureTextNodeSize(Node, 0.0f);
		return Trace;
	}
}

#endif
