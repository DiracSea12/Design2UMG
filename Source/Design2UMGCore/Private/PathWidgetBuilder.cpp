// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "PathWidgetBuilder.h"

#include "Design2UMGLog.h"
#include "DesignTypeMapper.h"
#include "ImageFillUtils.h"

namespace
{
	void ApplyPathSizeOverrides(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		Widget.Properties.Add(TEXT("WidthOverride"), FString::FromInt(FMath::Max(1, FMath::RoundToInt(Node->Width.Value))));
		Widget.Properties.Add(TEXT("HeightOverride"), FString::FromInt(FMath::Max(1, FMath::RoundToInt(Node->Height.Value))));
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

		const FString* TexturePath = Node->Metadata.Find(TEXT("figma.vectorRasterFallback"));
		return TexturePath ? *TexturePath : FString();
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
		Widget.Properties.Add(TEXT("TexturePath"), VectorRasterFallback);
		ApplyPathSizeOverrides(Widget, Node);
		return Widget;
	}

	if (Node->Fill.Type == EDesignFillType::Image && !Node->Fill.Image.Url.IsEmpty())
	{
		Widget.Type = TEXT("Image");
		Widget.Properties.Add(TEXT("TexturePath"), Node->Fill.Image.Url);
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
			Widget.Properties.Add(TEXT("PathGeometry"), Node->Geometry);
			Widget.Properties.Add(TEXT("PathFillRule"), Node->FillRule);
			if (!FillColor.IsEmpty())
			{
				Widget.Properties.Add(TEXT("PathFillColor"), FillColor);
			}
			if (!StrokeColor.IsEmpty() && StrokeThickness > 0.0f)
			{
				Widget.Properties.Add(TEXT("PathStrokeColor"), StrokeColor);
				Widget.Properties.Add(TEXT("PathStrokeThickness"), FString::SanitizeFloat(StrokeThickness));
				Widget.Properties.Add(TEXT("PathStrokeCap"), ResolvePathStrokeMetadata(Node, TEXT("stroke.cap"), TEXT("butt")));
				Widget.Properties.Add(TEXT("PathStrokeJoin"), ResolvePathStrokeMetadata(Node, TEXT("stroke.join"), TEXT("miter")));
			}
			ApplyPathSizeOverrides(Widget, Node);
			return Widget;
		}
	}

	Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->Layout);
	return Widget;
}
