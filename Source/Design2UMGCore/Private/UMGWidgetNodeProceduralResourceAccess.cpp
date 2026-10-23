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
			Result.Gradient = Node.Brush.Gradient;
		}
		else if (Node.Brush.MaterialRef.IsSet())
		{
			Result.MaterialRef = Node.Brush.MaterialRef.GetValue();
			Result.Shape = Node.Brush.Shape;
			if (!Result.Shape.IsSet() && Node.Brush.MaterialParams.IsSet())
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
        if (Result.Gradient.Mesh.IsSet() || Node.Brush.Shader.IsSet())
        {
            if (!Result.WidthOverride.IsSet() && Node.Bounds.W > 0) Result.WidthOverride = Node.Bounds.W;
            if (!Result.HeightOverride.IsSet() && Node.Bounds.H > 0) Result.HeightOverride = Node.Bounds.H;
        }
        if(Node.Brush.Shader.IsSet())
        {
            if(!Result.WidthOverride.IsSet()&&Node.Brush.Shader->Size.X>0)Result.WidthOverride=Node.Brush.Shader->Size.X;
            if(!Result.HeightOverride.IsSet()&&Node.Brush.Shader->Size.Y>0)Result.HeightOverride=Node.Brush.Shader->Size.Y;
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
