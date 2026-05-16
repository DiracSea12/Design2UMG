// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetNodeProceduralResourceAccess.h"

#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"

namespace Design2UMGWidgetNodeProceduralResourceAccess
{
	bool TryBuildProceduralResourceRequest(
		const FUMGWidgetNode& Node,
		FUMGProceduralResourceRequest& OutRequest)
	{
		FUMGProceduralResourceRequest Result;
		if (!Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialRef, Result.MaterialRef))
		{
			return false;
		}

		FString MaterialParams;
		if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Node, UMGPropertyKeys::MaterialParams, MaterialParams))
		{
			Result.MaterialParams = MaterialParams;
		}

		float FloatValue = 0.0f;
		if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::WidthOverride, FloatValue))
		{
			Result.WidthOverride = FloatValue;
		}
		if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::HeightOverride, FloatValue))
		{
			Result.HeightOverride = FloatValue;
		}
		if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Node, UMGPropertyKeys::ShadowBlur, FloatValue))
		{
			Result.ShadowBlur = FloatValue;
		}

		FLinearColor ColorValue = FLinearColor::Transparent;
		if (Design2UMGWidgetNodeAccess::TryGetWidgetColor(Node, UMGPropertyKeys::BrushColor, ColorValue))
		{
			Result.BrushColor = ColorValue;
		}

		OutRequest = MoveTemp(Result);
		return true;
	}
}
