// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "CoreMinimal.h"
#include "VisualMapper.h"

struct FDesignNode;
struct FUMGWidgetNode;

namespace VisualMapperFillTransfer
{
	FUMGWidgetNode WrapWithGradientStroke(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node);
	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node);
	FUMGWidgetNode WrapWithMultiFill(
		FUMGWidgetNode&& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& Options);
}
