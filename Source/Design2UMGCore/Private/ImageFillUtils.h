#pragma once

#include "DesignSchema.h"
#include "UMGSchema.h"

namespace ImageFillUtils
{
	inline void ApplyImageFillProperties(FUMGWidgetNode& Widget, const FDesignImageFill& Image)
	{
		if (Image.Mode == EDesignImageMode::Crop && !Image.Transform.IsIdentity())
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Crop"));
			Widget.Properties.Add(TEXT("ImageTransform"),
				FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
					Image.Transform.A, Image.Transform.B, Image.Transform.TX,
					Image.Transform.C, Image.Transform.D, Image.Transform.TY));
		}
		else if (Image.Mode == EDesignImageMode::Fill)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fill"));
		}
		else if (Image.Mode == EDesignImageMode::Fit)
		{
			Widget.Properties.Add(TEXT("ImageMode"), TEXT("Fit"));
		}

		if (Image.Opacity < 1.0f)
		{
			Widget.Properties.Add(TEXT("RenderOpacity"), FString::SanitizeFloat(Image.Opacity));
		}
	}
}
