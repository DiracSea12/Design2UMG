// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PathWidgetBuilder.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "ImageFillUtils.h"
#include "UMGValueParser.h"

namespace
{
	void ApplyPathSizeOverrides(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		Widget.WidgetStyle.WidthOverride = FMath::Max(1, FMath::RoundToInt(Node->Width.Value));
		Widget.WidgetStyle.bHasWidthOverride = true;
		Widget.WidgetStyle.HeightOverride = FMath::Max(1, FMath::RoundToInt(Node->Height.Value));
		Widget.WidgetStyle.bHasHeightOverride = true;
	}

	bool HasGradientPathFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		if (Node->Fill.Type == EDesignFillType::Gradient)
		{
			return true;
		}

		for (const FDesignFill& Fill : Node->Fills)
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

		return Node->FigmaVectorRasterFallbackImageRef;
	}

	FString ResolvePathRasterFillColor(const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Fill.Type == EDesignFillType::Color)
		{
			return Node->Fill.SolidColor;
		}

		if (Node->Fill.Type == EDesignFillType::Gradient && Node->Fill.Gradient.Colors.Num() > 0)
		{
			return Node->Fill.Gradient.Colors[0].Color;
		}

		if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EDesignFillType::Color)
		{
			return Node->Stroke.Fill.SolidColor;
		}

		for (const FDesignFill& Fill : Node->Fills)
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
		if (Node->Stroke.HasStroke() && Node->Stroke.Fill.Type == EDesignFillType::Color)
		{
			return Node->Stroke.Fill.SolidColor;
		}

		return FString();
	}

	float ResolvePathRasterStrokeThickness(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Stroke.HasStroke())
		{
			return 0.0f;
		}

		if (Node->Stroke.Thickness.bUniform)
		{
			return Node->Stroke.Thickness.Uniform;
		}

		return FMath::Max(
			FMath::Max(Node->Stroke.Thickness.Top, Node->Stroke.Thickness.Right),
			FMath::Max(Node->Stroke.Thickness.Bottom, Node->Stroke.Thickness.Left));
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

	if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Brush.TexturePath = Node->Fill.Image.Url;
		Widget.Brush.Image.TexturePath = Node->Fill.Image.Url;
		Widget.Brush.Image.bHasTexturePath = true;
		ImageFillUtils::ApplyImageFillProperties(Widget, Node->Fill.Image);
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	if (!Node->Geometry.IsEmpty())
	{
		const FString FillColor = ResolvePathRasterFillColor(Node);
		const FString StrokeColor = ResolvePathRasterStrokeColor(Node);
		const float StrokeThickness = ResolvePathRasterStrokeThickness(Node);
		if (!FillColor.IsEmpty() || (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f))
		{
			UE_LOG(LogDesign2UMG, Log, TEXT("CreatePathWidget: '%s' rasterize %dx%d (raw=%.1fx%.1f) geometry=%d chars fill=%d stroke=%.2f"),
				*Node->Name,
				FMath::Max(1, FMath::RoundToInt(Node->Width.Value)),
				FMath::Max(1, FMath::RoundToInt(Node->Height.Value)),
				Node->Width.Value, Node->Height.Value, Node->Geometry.Len(),
				FillColor.IsEmpty() ? 0 : 1,
				StrokeThickness);

			Widget.Type = TEXT("Image");
			Widget.Path.Geometry = Node->Geometry;
			Widget.Path.bHasGeometry = true;
			Widget.Path.FillRule = Node->FillRule;
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

	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}
