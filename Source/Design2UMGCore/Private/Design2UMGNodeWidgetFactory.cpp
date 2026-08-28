// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGNodeWidgetFactory.h"

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

	void AppendDashedCapsule(
		FString& Geometry,
		const bool bVertical,
		const float CrossExtent,
		const float SegmentStart,
		const float SegmentEnd,
		const bool bRoundCap)
	{
		const float SegmentLength = SegmentEnd - SegmentStart;
		if (SegmentLength <= KINDA_SMALL_NUMBER)
		{
			return;
		}

		if (!Geometry.IsEmpty())
		{
			Geometry += TEXT(" ");
		}

		if (!bRoundCap)
		{
			Geometry += bVertical
				? FString::Printf(TEXT("M 0,%.4f L %.4f,%.4f L %.4f,%.4f L 0,%.4f Z"),
					SegmentStart, CrossExtent, SegmentStart, CrossExtent, SegmentEnd, SegmentEnd)
				: FString::Printf(TEXT("M %.4f,0 L %.4f,0 L %.4f,%.4f L %.4f,%.4f Z"),
					SegmentStart, SegmentEnd, SegmentEnd, CrossExtent, SegmentStart, CrossExtent);
			return;
		}

		const float Radius = FMath::Min(CrossExtent * 0.5f, SegmentLength * 0.5f);
		if (bVertical)
		{
			const float CenterX = CrossExtent * 0.5f;
			const float Left = CenterX - Radius;
			const float Right = CenterX + Radius;
			Geometry += FString::Printf(
				TEXT("M %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f L %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f L %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f Z"),
				CenterX, SegmentStart,
				Radius, Radius, Right, SegmentStart + Radius,
				Right, SegmentEnd - Radius,
				Radius, Radius, CenterX, SegmentEnd,
				Radius, Radius, Left, SegmentEnd - Radius,
				Left, SegmentStart + Radius,
				Radius, Radius, CenterX, SegmentStart);
		}
		else
		{
			const float CenterY = CrossExtent * 0.5f;
			const float Top = CenterY - Radius;
			const float Bottom = CenterY + Radius;
			Geometry += FString::Printf(
				TEXT("M %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f L %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f L %.4f,%.4f A %.4f,%.4f 0 0,1 %.4f,%.4f Z"),
				SegmentStart, CenterY,
				Radius, Radius, SegmentStart + Radius, Top,
				SegmentEnd - Radius, Top,
				Radius, Radius, SegmentEnd, CenterY,
				Radius, Radius, SegmentEnd - Radius, Bottom,
				SegmentStart + Radius, Bottom,
				Radius, Radius, SegmentStart, CenterY);
		}
	}

	TSharedPtr<FDesignNode> BuildDashedLineFallbackPathNode(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| !Node->Shape.Geometry.IsEmpty()
			|| !Node->Visual.Stroke.HasStroke()
			|| !Node->Visual.Stroke.Fill.IsSet())
		{
			return nullptr;
		}

		if (Node->Visual.Stroke.DashPattern.Num() == 0 || !Node->Transform.LayoutBoundsSize.IsSet())
		{
			return nullptr;
		}

		const FVector2D LayoutBoundsSize = Node->Transform.LayoutBoundsSize.GetValue();
		const float LayoutWidth = LayoutBoundsSize.X;
		const float LayoutHeight = LayoutBoundsSize.Y;
		constexpr float DegenerateAxisTolerance = 0.01f;
		const bool bVertical = FMath::Abs(LayoutWidth) <= DegenerateAxisTolerance
			&& LayoutHeight > DegenerateAxisTolerance;
		const bool bHorizontal = FMath::Abs(LayoutHeight) <= DegenerateAxisTolerance
			&& LayoutWidth > DegenerateAxisTolerance;
		if (!bVertical && !bHorizontal)
		{
			return nullptr;
		}

		TArray<float> DashPattern = Node->Visual.Stroke.DashPattern;
		if (DashPattern.Num() % 2 != 0)
		{
			const TArray<float> RepeatedPattern = DashPattern;
			DashPattern.Append(RepeatedPattern);
		}

		const float MajorExtent = bVertical ? Node->Transform.Height.Value : Node->Transform.Width.Value;
		const float LayoutMajorExtent = bVertical ? LayoutHeight : LayoutWidth;
		const float DashScale = MajorExtent / FMath::Max(LayoutMajorExtent, KINDA_SMALL_NUMBER);
		const float StrokeThickness = Node->Visual.Stroke.Thickness.bUniform
			? Node->Visual.Stroke.Thickness.Uniform
			: FMath::Max(
				FMath::Max(Node->Visual.Stroke.Thickness.Top, Node->Visual.Stroke.Thickness.Right),
				FMath::Max(Node->Visual.Stroke.Thickness.Bottom, Node->Visual.Stroke.Thickness.Left));
		if (StrokeThickness <= KINDA_SMALL_NUMBER || MajorExtent <= KINDA_SMALL_NUMBER)
		{
			return nullptr;
		}

		const bool bRoundCap = Node->Visual.Stroke.Cap == EDesignStrokeCap::Round;
		FString Geometry;
		float Cursor = 0.0f;
		int32 PatternIndex = 0;
		bool bDrawSegment = true;
		for (int32 SegmentIndex = 0; Cursor < MajorExtent && SegmentIndex < 4096; ++SegmentIndex)
		{
			const float PatternLength = DashPattern[PatternIndex] * DashScale;
			const float SegmentEnd = FMath::Min(MajorExtent, Cursor + PatternLength);
			if (bDrawSegment)
			{
				AppendDashedCapsule(Geometry, bVertical, StrokeThickness, Cursor, SegmentEnd, bRoundCap);
			}
			Cursor = SegmentEnd;
			PatternIndex = (PatternIndex + 1) % DashPattern.Num();
			bDrawSegment = !bDrawSegment;
		}

		TSharedPtr<FDesignNode> FallbackNode = BuildPathFallbackNode(Node, Geometry);
		if (!FallbackNode.IsValid())
		{
			return nullptr;
		}

		FallbackNode->Visual.Fills.Reset();
		FallbackNode->Visual.Fills.Add(Node->Visual.Stroke.Fill.GetValue());
		FallbackNode->Visual.Stroke = FDesignStroke();
		return FallbackNode;
	}

	bool HasGradientVisualFill(const TSharedPtr<FDesignNode>& Node)
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

	FUMGWidgetNode CreateContainerWidget(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Widget;
		Widget.Type = DesignTypeMapper::MapNodeTypeToWidget(Node->Type, Node->LayoutData.Layout);
		if (Node.IsValid())
		{
			if (Node->LayoutData.Overflow == EDesignOverflow::VerticalScrolling)
			{
				Widget.Type = TEXT("ScrollBox");
				Widget.ScrollBox.Orientation = TEXT("Vertical");
				Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");
			}
			else if (Node->LayoutData.Overflow == EDesignOverflow::HorizontalScrolling)
			{
				Widget.Type = TEXT("ScrollBox");
				Widget.ScrollBox.Orientation = TEXT("Horizontal");
				Widget.ScrollBox.ScrollBarVisibility = TEXT("Hidden");
			}
		}
		return Widget;
	}

	FUMGWidgetNode CreateEllipseWidget(const TSharedPtr<FDesignNode>& Node)
	{
		const float Width = Node->Transform.Width.IsFixed() ? Node->Transform.Width.Value : 0.0f;
		const float Height = Node->Transform.Height.IsFixed() ? Node->Transform.Height.Value : 0.0f;
		const FDesignFill& PrimaryFill = Node->Visual.PrimaryFill();
		if (PrimaryFill.IsImage()
			&& !Node->Visual.VectorRasterFallbackImageRef.IsEmpty()
			&& Node->Visual.bRenderBoundsClippedByParent)
		{
			return PathWidgetBuilder::CreatePathWidget(Node);
		}

		const bool bHasGradientFill = HasGradientVisualFill(Node);
		const bool bHasVectorRasterFallback =
			bHasGradientFill && !Node->Visual.VectorRasterFallbackImageRef.IsEmpty();
		const bool bHasStroke = Node->Visual.Stroke.HasStroke();

		if (Width > 0.0f
			&& Height > 0.0f
			&& ((PrimaryFill.IsColor() && PrimaryFill.bEnabled && !bHasStroke)
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
			VisualMapper::ApplyRoundedCorner(Widget, EllipseRadius);
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
				if (Node->Name.IsEmpty())
				{
					Result.Warnings.Add(FString::Printf(
						TEXT("Figma TEXT_PATH [id=%s] -> TextBlock (text-on-path downgraded to regular text)"),
						*Node->Id));
				}
				else
				{
					Result.Warnings.Add(FString::Printf(
						TEXT("Figma TEXT_PATH '%s' [id=%s] -> TextBlock (text-on-path downgraded to regular text)"),
						*Node->Name,
						*Node->Id));
				}
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
			Result.Warnings.Append(IconFontResult.Warnings);
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

		UE_LOG(LogDesign2UMG, Warning, TEXT("Polygon '%s' has no PathGeometry; falling back to Border."), *Node->Name);
		Result.Warnings.Add(FString::Printf(TEXT("Polygon '%s' -> Border (missing PathGeometry)"), *Node->Name));
		Result.Widget = CreateContainerWidget(Node);
		return Result;

	case EDesignNodeType::Path:
		if (Node->Shape.Geometry.IsEmpty())
		{
			if (TSharedPtr<FDesignNode> PathNode = BuildDashedLineFallbackPathNode(Node))
			{
				Result.Widget = PathWidgetBuilder::CreatePathWidget(PathNode);
				return Result;
			}
		}
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
