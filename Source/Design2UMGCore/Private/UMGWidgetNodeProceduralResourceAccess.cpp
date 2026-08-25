// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "UMGWidgetNodeProceduralResourceAccess.h"

#include "UMGWidgetNodeAccess.h"

namespace Design2UMGWidgetNodeProceduralResourceAccess
{
	bool TryBuildProceduralResourceRequest(
		const FUMGWidgetNode& Node,
		FUMGProceduralResourceRequest& OutRequest)
	{
		// Direct typed reads with descriptor-identical presence rules.
		FUMGProceduralResourceRequest Result;
		if (Node.Brush.Gradient.HasAny())
		{
			Result.MaterialRef = Design2UMGWidgetNodeAccess::GetGradientMaterialRef(Node.Brush.Gradient);
			Result.MaterialParams = Design2UMGWidgetNodeAccess::SerializeGradientMaterialParams(Node.Brush.Gradient);
		}
		else if (Node.Brush.MaterialRef.IsSet())
		{
			Result.MaterialRef = Node.Brush.MaterialRef.GetValue();
			if (Node.Brush.MaterialParams.IsSet())
			{
				Result.MaterialParams = Node.Brush.MaterialParams.GetValue();
			}
		}
		else
		{
			return false;
		}

		if (Node.WidgetStyle.WidthOverride.IsSet())
		{
			Result.WidthOverride = Node.WidgetStyle.WidthOverride.GetValue();
		}
		if (Node.WidgetStyle.HeightOverride.IsSet())
		{
			Result.HeightOverride = Node.WidgetStyle.HeightOverride.GetValue();
		}
		if (Node.Shadow.ShadowBlur.IsSet())
		{
			Result.ShadowBlur = Node.Shadow.ShadowBlur.GetValue();
		}

		if (Node.Brush.BrushColor.IsSet())
		{
			Result.BrushColor = Node.Brush.BrushColor.GetValue();
		}

		OutRequest = MoveTemp(Result);
		return true;
	}
}
