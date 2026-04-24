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
	TSharedPtr<FDesignNode> BuildPathFallbackNode(const TSharedPtr<FDesignNode>& Node, const FString& Geometry)
	{
		if (!Node.IsValid() || Geometry.IsEmpty())
		{
			return nullptr;
		}

		// Geometry fallback must stay side-effect-free for the caller-owned design node.
		TSharedPtr<FDesignNode> FallbackNode = MakeShared<FDesignNode>(*Node);
		FallbackNode->Shape.Geometry = Geometry;
		return FallbackNode;
	}

	TSharedPtr<FDesignNode> BuildEllipseFallbackPathNode(const TSharedPtr<FDesignNode>& Node, const float Width, const float Height)
	{
		const float RadiusX = Width / 2.0f;
		const float RadiusY = Height / 2.0f;
		return BuildPathFallbackNode(
			Node,
			FString::Printf(
				TEXT("M 0,%.2f A %.2f,%.2f 0 1,1 %.2f,%.2f A %.2f,%.2f 0 1,1 0,%.2f Z"),
				RadiusY,
				RadiusX,
				RadiusY,
				Width,
				RadiusY,
				RadiusX,
				RadiusY,
				RadiusY));
	}

	TSharedPtr<FDesignNode> BuildLineFallbackPathNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return nullptr;
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		return BuildPathFallbackNode(
			Node,
			FString::Printf(TEXT("M 0,%.1f L %.1f,%.1f"), Height * 0.5f, Width, Height * 0.5f));
	}

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
			if (TSharedPtr<FDesignNode> PathNode = BuildEllipseFallbackPathNode(Node, Width, Height))
			{
				return PathWidgetBuilder::CreatePathWidget(PathNode);
			}
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

Design2UMGNodeWidgetFactory::FBuildResult Design2UMGNodeWidgetFactory::CreateWidgetForNodeType(
	const TSharedPtr<FDesignNode>& Node,
	const FBuildArgs& Args)
{
	FBuildResult Result;

	switch (Node->Type)
	{
	case EDesignNodeType::Text:
		{
			Result.Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node);
			if (Design2UMGSourceMetadata::IsSourceNodeType(Node, TEXT("TEXT_PATH")))
			{
				const FString NodeLabel = Node->Name.IsEmpty() ? Node->Id : Node->Name;
				Result.Warnings.Add(FString::Printf(
					TEXT("Figma TEXT_PATH '%s' → TextBlock (text-on-path 降级为普通文本)"),
					*NodeLabel));
			}
			return Result;
		}

	case EDesignNodeType::IconFont:
		{
			Design2UMGTextWidgetBuilder::FIconFontBuildArgs IconFontArgs;
			IconFontArgs.ExistingWarnedFamilies = Args.ExistingWarnedFamilies;
			IconFontArgs.SettingsSnapshot = Args.IconFontSettingsSnapshot;

			Design2UMGTextWidgetBuilder::FIconFontBuildResult IconFontResult =
				Design2UMGTextWidgetBuilder::CreateIconFontWidget(Node, IconFontArgs);
			Result.Widget = MoveTemp(IconFontResult.Widget);
			Result.NewlyWarnedFamilies = MoveTemp(IconFontResult.NewlyWarnedFamilies);
			Result.PrunedInvalidMappings = MoveTemp(IconFontResult.PrunedInvalidMappings);
			Result.PendingIconFontMappings = MoveTemp(IconFontResult.PendingIconFontMappings);
			return Result;
		}

	case EDesignNodeType::Frame:
	case EDesignNodeType::Group:
	case EDesignNodeType::Rectangle:
		Result.Widget = CreateContainerWidget(Node);
		return Result;

	case EDesignNodeType::Ellipse:
		Result.Widget = CreateEllipseWidget(Node);
		return Result;

	case EDesignNodeType::Polygon:
		if (!Node->Shape.Geometry.IsEmpty())
		{
			Result.Widget = PathWidgetBuilder::CreatePathWidget(Node);
			return Result;
		}

		UE_LOG(LogDesign2UMG, Warning, TEXT("多边形 '%s' 无 PathGeometry，降级为 Border"), *Node->Name);
		Result.Warnings.Add(FString::Printf(TEXT("Polygon '%s' → Border (无 PathGeometry)"), *Node->Name));
		Result.Widget = CreateContainerWidget(Node);
		return Result;

	case EDesignNodeType::Path:
		Result.Widget = PathWidgetBuilder::CreatePathWidget(Node);
		return Result;

	case EDesignNodeType::Line:
		if (Node->Shape.Geometry.IsEmpty())
		{
			if (TSharedPtr<FDesignNode> PathNode = BuildLineFallbackPathNode(Node))
			{
				Result.Widget = PathWidgetBuilder::CreatePathWidget(PathNode);
				return Result;
			}
		}
		Result.Widget = PathWidgetBuilder::CreatePathWidget(Node);
		return Result;

	default:
		Result.Widget = CreateContainerWidget(Node);
		return Result;
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
