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
			Widget.Brush.Image.bHasTexturePath = true;
		}

		if (Image.Mode == EDesignImageMode::Crop && !Image.Transform.IsIdentity())
		{
			Widget.Brush.Image.Mode = TEXT("Crop");
			Widget.Brush.Image.bHasMode = true;
			Widget.Brush.Image.Transform.A = Image.Transform.A;
			Widget.Brush.Image.Transform.B = Image.Transform.B;
			Widget.Brush.Image.Transform.TX = Image.Transform.TX;
			Widget.Brush.Image.Transform.C = Image.Transform.C;
			Widget.Brush.Image.Transform.D = Image.Transform.D;
			Widget.Brush.Image.Transform.TY = Image.Transform.TY;
			Widget.Brush.Image.bHasTransform = true;
		}
		else if (Image.Mode == EDesignImageMode::Fill)
		{
			Widget.Brush.Image.Mode = TEXT("Fill");
			Widget.Brush.Image.bHasMode = true;
		}
		else if (Image.Mode == EDesignImageMode::Fit)
		{
			Widget.Brush.Image.Mode = TEXT("Fit");
			Widget.Brush.Image.bHasMode = true;
		}

		if (Image.Opacity < 1.0f)
		{
			Widget.Render.RenderOpacity = Image.Opacity;
			Widget.Render.bHasRenderOpacity = true;
		}
	}
}
