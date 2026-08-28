// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PathWidgetBuilder.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "ImageFillUtils.h"
#include "UMGValueParser.h"

namespace
{
	FString GetStrokeCapString(const EDesignStrokeCap Cap)
	{
		switch (Cap)
		{
		case EDesignStrokeCap::Round:
			return TEXT("round");
		case EDesignStrokeCap::Square:
			return TEXT("square");
		default:
			return TEXT("butt");
		}
	}

	FString GetStrokeJoinString(const EDesignStrokeJoin Join)
	{
		switch (Join)
		{
		case EDesignStrokeJoin::Round:
			return TEXT("round");
		case EDesignStrokeJoin::Bevel:
			return TEXT("bevel");
		default:
			return TEXT("miter");
		}
	}

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

		return Node->Visual.VectorRasterFallbackImageRef;
	}

	FString ResolvePathRasterFillColor(const TSharedPtr<FDesignNode>& Node)
	{
		const FDesignFill& PrimaryFill = Node->Visual.PrimaryFill();
		if (PrimaryFill.Type == EDesignFillType::Color)
		{
			return PrimaryFill.SolidColor;
		}

		if (PrimaryFill.Type == EDesignFillType::Gradient && PrimaryFill.Gradient.Colors.Num() > 0)
		{
			return PrimaryFill.Gradient.Colors[0].Color;
		}

		if (Node->Visual.Stroke.HasStroke()
			&& Node->Visual.Stroke.Fill.IsSet()
			&& Node->Visual.Stroke.Fill->Type == EDesignFillType::Color)
		{
			return Node->Visual.Stroke.Fill->SolidColor;
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
		if (Node->Visual.Stroke.HasStroke()
			&& Node->Visual.Stroke.Fill.IsSet()
			&& Node->Visual.Stroke.Fill->Type == EDesignFillType::Color)
		{
			return Node->Visual.Stroke.Fill->SolidColor;
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

}

FUMGWidgetNode PathWidgetBuilder::CreatePathWidget(const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Widget;

	if (const FString VectorRasterFallback = GetVectorRasterFallbackTexturePath(Node); !VectorRasterFallback.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Brush.Image.TexturePath = VectorRasterFallback;
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	const FDesignFill& PrimaryFill = Node->Visual.PrimaryFill();
	if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Brush.Image.TexturePath = PrimaryFill.Image.Url;
		ImageFillUtils::ApplyImageFillProperties(Widget, PrimaryFill.Image);
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
			Widget.Path.FillRule = Node->Shape.FillRule;
			if (!FillColor.IsEmpty())
			{
				Widget.Path.FillColor = FUMGValueParser::ParseColor(FillColor);
			}
			if (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f)
			{
				Widget.Path.StrokeColor = FUMGValueParser::ParseColor(StrokeColor);
				Widget.Path.StrokeThickness = StrokeThickness;
				Widget.Path.StrokeCap = GetStrokeCapString(Node->Visual.Stroke.Cap);
				Widget.Path.StrokeJoin = GetStrokeJoinString(Node->Visual.Stroke.Join);
			}
			ApplyPathSizeOverrides(Widget, Node);
			return Widget;
		}
	}

	Widget.Type = DesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->LayoutData.Layout);
	return Widget;
}
