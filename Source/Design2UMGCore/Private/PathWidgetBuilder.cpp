// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PathWidgetBuilder.h"
#include "DesignSchemaSourceExtensions.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "ImageFillUtils.h"
#include "UMGValueParser.h"

namespace
{
	void ApplyPathSizeOverrides(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		Widget.WidgetStyle.WidthOverride = FMath::Max(1, FMath::RoundToInt(Node->Transform.Width.Value));
		Widget.WidgetStyle.HeightOverride = FMath::Max(1, FMath::RoundToInt(Node->Transform.Height.Value));
	}

	bool HasGradientPathFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Visual.Fill.Type == EDesignFillType::Gradient)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (Fill.Type == EDesignFillType::Gradient)
			{
				return true;
			}
		}

		return false;
	}

	FString GetVectorRasterFallbackTexturePath(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || Node->Type != EDesignNodeType::Ellipse || !HasGradientPathFill(Node))
		{
			return FString();
		}

		return Node->Source().VectorRasterFallbackImageRef;
	}

	FString ResolvePathRasterFillColor(const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Visual.Fill.Type == EDesignFillType::Color)
		{
			return Node->Visual.Fill.SolidColor;
		}

		if (Node->Visual.Fill.Type == EDesignFillType::Gradient && Node->Visual.Fill.Gradient.Colors.Num() > 0)
		{
			return Node->Visual.Fill.Gradient.Colors[0].Color;
		}

		if (Node->Visual.Stroke.HasStroke() && Node->Visual.Stroke.Fill.Type == EDesignFillType::Color)
		{
			return Node->Visual.Stroke.Fill.SolidColor;
		}

		for (const FDesignFill& Fill : Node->Visual.Fills)
		{
			if (Fill.Type == EDesignFillType::Color)
			{
				return Fill.SolidColor;
			}

			if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
			{
				return Fill.Gradient.Colors[0].Color;
			}
		}

		return FString();
	}

	FString ResolvePathRasterStrokeColor(const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Visual.Stroke.HasStroke() && Node->Visual.Stroke.Fill.Type == EDesignFillType::Color)
		{
			return Node->Visual.Stroke.Fill.SolidColor;
		}

		return FString();
	}

	float ResolvePathRasterStrokeThickness(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Visual.Stroke.HasStroke())
		{
			return 0.0f;
		}

		if (Node->Visual.Stroke.Thickness.bUniform)
		{
			return Node->Visual.Stroke.Thickness.Uniform;
		}

		return FMath::Max(
			FMath::Max(Node->Visual.Stroke.Thickness.Top, Node->Visual.Stroke.Thickness.Right),
			FMath::Max(Node->Visual.Stroke.Thickness.Bottom, Node->Visual.Stroke.Thickness.Left));
	}

	FString ResolvePathStrokeMetadata(const TSharedPtr<FDesignNode>& Node, const TCHAR* Key, const TCHAR* DefaultValue)
	{
		if (!Node.IsValid())
		{
			return FString(DefaultValue);
		}

		const FString* Value = Node->Metadata.Find(Key);
		return (Value && !Value->IsEmpty()) ? *Value : FString(DefaultValue);
	}
}

FUMGWidgetNode PathWidgetBuilder::CreatePathWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	if (const FString VectorRasterFallback = GetVectorRasterFallbackTexturePath(Node); !VectorRasterFallback.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Brush.TexturePath = VectorRasterFallback;
		Widget.Brush.Image.TexturePath = VectorRasterFallback;
		Widget.Brush.Image.bHasTexturePath = true;
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	if (Node->Visual.Fill.Type == EDesignFillType::Image && !Node->Visual.Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Brush.TexturePath = Node->Visual.Fill.Image.Url;
		Widget.Brush.Image.TexturePath = Node->Visual.Fill.Image.Url;
		Widget.Brush.Image.bHasTexturePath = true;
		ImageFillUtils::ApplyImageFillProperties(Widget, Node->Visual.Fill.Image);
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	if (!Node->Shape.Geometry.IsEmpty())
	{
		const FString FillColor = ResolvePathRasterFillColor(Node);
		const FString StrokeColor = ResolvePathRasterStrokeColor(Node);
		const float StrokeThickness = ResolvePathRasterStrokeThickness(Node);
		if (!FillColor.IsEmpty() || (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("CreatePathWidget: '%s' rasterize %dx%d (raw=%.1fx%.1f) geometry=%d chars fill=%d stroke=%.2f"),
				*Node->Name,
				FMath::Max(1, FMath::RoundToInt(Node->Transform.Width.Value)),
				FMath::Max(1, FMath::RoundToInt(Node->Transform.Height.Value)),
				Node->Transform.Width.Value, Node->Transform.Height.Value, Node->Shape.Geometry.Len(),
				FillColor.IsEmpty() ? 0 : 1,
				StrokeThickness);

			Widget.Type = TEXT("Image");
			Widget.Path.Geometry = Node->Shape.Geometry;
			Widget.Path.bHasGeometry = true;
			Widget.Path.FillRule = Node->Shape.FillRule;
			Widget.Path.bHasFillRule = true;
			if (!FillColor.IsEmpty())
			{
				Widget.Path.FillColor = FUMGValueParser::ParseColor(FillColor);
				Widget.Path.bHasFillColor = true;
			}
			if (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f)
			{
				Widget.Path.StrokeColor = FUMGValueParser::ParseColor(StrokeColor);
				Widget.Path.bHasStrokeColor = true;
				Widget.Path.StrokeThickness = StrokeThickness;
				Widget.Path.bHasStrokeThickness = true;
				Widget.Path.StrokeCap = ResolvePathStrokeMetadata(Node, TEXT("stroke.cap"), TEXT("butt"));
				Widget.Path.bHasStrokeCap = true;
				Widget.Path.StrokeJoin = ResolvePathStrokeMetadata(Node, TEXT("stroke.join"), TEXT("miter"));
				Widget.Path.bHasStrokeJoin = true;
			}
			ApplyPathSizeOverrides(Widget, Node);
			return Widget;
		}
	}

	Widget.Type = DesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->LayoutData.Layout);
	return Widget;
}
