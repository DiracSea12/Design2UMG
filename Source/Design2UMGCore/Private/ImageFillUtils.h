// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#pragma once

#include "DesignSchema.h"
#include "UMGSchema.h"

namespace ImageFillUtils
{
	inline void ApplyImageFillProperties(FUMGWidgetNode& Widget, const FDesignImageFill& Image)
	{
		if (!Image.Url.IsEmpty())
		{
			Widget.Brush.Image.TexturePath = Image.Url;
		}

		if (Image.Mode == EDesignImageMode::Crop && !Image.Transform.IsIdentity())
		{
			Widget.Brush.Image.Mode = TEXT("Crop");
			FUMGVisualBrushData::FImageTransformData Transform;
			Transform.A = Image.Transform.A;
			Transform.B = Image.Transform.B;
			Transform.TX = Image.Transform.TX;
			Transform.C = Image.Transform.C;
			Transform.D = Image.Transform.D;
			Transform.TY = Image.Transform.TY;
			Widget.Brush.Image.Transform = Transform;
		}
		else if (Image.Mode == EDesignImageMode::Fill)
		{
			Widget.Brush.Image.Mode = TEXT("Fill");
		}
		else if (Image.Mode == EDesignImageMode::Fit)
		{
			Widget.Brush.Image.Mode = TEXT("Fit");
		}

		if (Image.Opacity < 1.0f)
		{
			Widget.Render.RenderOpacity = Image.Opacity;
		}
	}
}
