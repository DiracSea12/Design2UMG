// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGNodeWidgetFactory.h"
#include "DesignSchemaSourceExtensions.h"

#include "Design2UMGLog.h"
#include "Design2UMGSourceMetadata.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignTypeMapper.h"
#include "PathWidgetBuilder.h"
#include "VisualMapper.h"

namespace
{
	bool HasGradientVisualFill(const TSharedPtr<FDesignNode>& Node)
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

	FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Widget;
		Widget.Type = FDesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->LayoutData.Layout);
		if (Node.IsValid())
		{
			const FString& OverflowDirection = Node->Source().Prototype.OverflowDirection;
			if (OverflowDirection.Equals(TEXT("VERTICAL_SCROLLING"), ESearchCase::IgnoreCase))
			{
				Widget.Type = TEXT("ScrollBox");
				Widget.ScrollBox.Orientation = TEXT("Vertical");
				Widget.ScrollBox.bHasOrientation = true;
				Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");
				Widget.ScrollBox.bHasScrollBarVisibility = true;
			}
			else if (OverflowDirection.Equals(TEXT("HORIZONTAL_SCROLLING"), ESearchCase::IgnoreCase))
			{
				Widget.Type = TEXT("ScrollBox");
				Widget.ScrollBox.Orientation = TEXT("Horizontal");
				Widget.ScrollBox.bHasOrientation = true;
				Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");
				Widget.ScrollBox.bHasScrollBarVisibility = true;
			}
		}
		return Widget;
	}

	FUMGWidgetNode CreateEllipseWidget(const TSharedPtr<FDesignNode>& Node)
	{
		const float Width = Node->Transform.Width.IsFixed() ? Node->Transform.Width.Value : 0.0f;
		const float Height = Node->Transform.Height.IsFixed() ? Node->Transform.Height.Value : 0.0f;
		const bool bHasGradientFill = HasGradientVisualFill(Node);
		const bool bHasVectorRasterFallback =
			bHasGradientFill && !Node->Source().VectorRasterFallbackImageRef.IsEmpty();
		const bool bHasStroke = Node->Visual.Stroke.HasStroke();

		if (Width > 0.0f
			&& Height > 0.0f
			&& ((Node->Visual.Fill.IsColor() && Node->Visual.Fill.bEnabled && !bHasStroke)
				|| bHasVectorRasterFallback))
		{
			const float RadiusX = Width / 2.0f;
			const float RadiusY = Height / 2.0f;
			Node->Shape.Geometry = FString::Printf(
				TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
				RadiusY,
				RadiusX,
				RadiusY,
				Width,
				RadiusY,
				RadiusX,
				RadiusY,
				RadiusY);
			return PathWidgetBuilder::CreatePathWidget(Node);
		}

		FUMGWidgetNode Widget = CreateContainerWidget(Node);
		if (Width > 0.0f && Height > 0.0f)
		{
			FDesignCornerRadius EllipseRadius;
			EllipseRadius.bUniform = true;
			EllipseRadius.Uniform = FMath::Min(Width, Height) / 2.0f;
			FVisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
		}
		return Widget;
	}
}

FUMGWidgetNode Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(
	const TSharedPtr<FDesignNode>& Node,
	FBuildContext& Context)
{
	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		{
			FUMGWidgetNode Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
			if (Design2UMGSourceMetadata::IsSourceNodeType(Node, TEXT("TEXT_PATH")))
			{
				const FString NodeLabel = Node->Name.IsEmpty() ? Node->Id : Node->Name;
				Context.Warnings.Add(FString::Printf(
					TEXT("Figma TEXT_PATH '%s' → TextBlock (text-on-path 降级为普通文本)"),
					*NodeLabel));
			}
			return Widget;
		}

	case EDesignNodeType::IconFont:
		{
			Design2UMGTextWidgetBuilder::FIconFontBuildContext IconFontContext(
				Context.WarnedFamilies,
				Context.PrunedInvalidMappings,
				Context.PendingIconFontMappings);
			return Design2UMGTextWidgetBuilder::CreateIconFontWidget(Node, IconFontContext);
		}

	case EDesignNodeType::Frame:
	case EDesignNodeType::Group:
	case EDesignNodeType::Rectangle:
		return CreateContainerWidget(Node);

	case EDesignNodeType::Ellipse:
		return CreateEllipseWidget(Node);

	case EDesignNodeType::Polygon:
		if (!Node->Shape.Geometry.IsEmpty())
		{
			return PathWidgetBuilder::CreatePathWidget(Node);
		}

		UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
		Context.Warnings.Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		return CreateContainerWidget(Node);

	case EDesignNodeType::Path:
		return PathWidgetBuilder::CreatePathWidget(Node);

	case EDesignNodeType::Line:
		if (Node->Shape.Geometry.IsEmpty())
		{
			const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
			const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
			Node->Shape.Geometry = FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), Height * 0.5f, Width, Height * 0.5f);
		}
		return PathWidgetBuilder::CreatePathWidget(Node);

	default:
		return CreateContainerWidget(Node);
	}
}

void Design2UMGNodeWidgetFactory::PopulateWidgetIdentityAndBounds(
	FUMGWidgetNode& Widget,
	const TSharedPtr<FDesignNode>& Node)
{
	if (!Node.IsValid())
	{
		return;
	}

	Widget.Id = Node->Id;
	Widget.Name = Node->Name.IsEmpty() ? Node->Id : Node->Name;
	Widget.Bounds.X = Node->Transform.X;
	Widget.Bounds.Y = Node->Transform.Y;
	Widget.Bounds.W = Node->Transform.Width.Value;
	Widget.Bounds.H = Node->Transform.Height.Value;
}
