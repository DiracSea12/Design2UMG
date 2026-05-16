// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "DesignSchemaFoundation.h"
#include "UMGSchema.h"

struct FDesignNode;

namespace VisualMapperStrokeSupport
{
	FMargin GetStrokeHalfInsets(const FDesignStrokeThickness& Thickness);
	bool HasUniformRectStroke(const FDesignStroke& Stroke);
	bool NeedsExplicitSideStroke(const TSharedPtr<FDesignNode>& Node);
	bool HasAnyConvertedChildStrokeCoverage(const FUMGWidgetNode& Border);
	FUMGWidgetNode WrapWithExplicitSideStroke(FUMGWidgetNode&& Border, const TSharedPtr<FDesignNode>& Node);
}
