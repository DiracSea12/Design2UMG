// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGNodeWidgetFactory.h"

#include "Design2UMGLog.h"
#include "Design2UMGSourceMetadata.h"
#include "DesignSchemaSourceExtensions.h"
#include "Dom/JsonObject.h"
#include "Design2UMGTextWidgetBuilder.h"
#include "DesignTypeMapper.h"
#include "PathWidgetBuilder.h"
#include "VisualMapper.h"
#include "UMGWidgetTypes.h"

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
			if (Node->LayoutData.Overflow == EDesignOverflow::VerticalScrolling
				|| Node->LayoutData.Overflow == EDesignOverflow::HorizontalScrolling)
			{
				// ScrollBox stacking follows an explicit auto-layout axis.
				// Use the overflow axis only when layout has no single stacking axis.
				const bool bHasExplicitLayoutAxis =
					Node->LayoutData.Layout == EDesignLayout::Vertical
					|| Node->LayoutData.Layout == EDesignLayout::Horizontal;
				const bool bStacksVertically = bHasExplicitLayoutAxis
					? Node->LayoutData.Layout == EDesignLayout::Vertical
					: Node->LayoutData.Overflow == EDesignOverflow::VerticalScrolling;
				Widget.Type = UMGWidgetTypes::ScrollBox;
				Widget.ScrollBox.Orientation = bStacksVertically ? TEXT("Vertical") : TEXT("Horizontal");
				// Figma scroll frames draw no scrollbar and reserve no space for one.
				Widget.ScrollBox.ScrollBarVisibility = TEXT("Collapsed");
				// A scrolled source shifts its content against the scroll axis; that shift is the initial position.
				double ScrollOffset = 0.0;
				for (const TSharedPtr<FDesignNode>& Child : Node->Children)
				{
					if (!Child.IsValid() || !Child->Transform.bEnabled) continue;
					const FVector2D Position = Child->Transform.SourceLayoutPosition.Get(FVector2D(Child->Transform.X, Child->Transform.Y));
					ScrollOffset = FMath::Max(ScrollOffset, -(bStacksVertically ? Position.Y : Position.X));
				}
				if (ScrollOffset > 0.0) Widget.ScrollBox.ScrollOffset = static_cast<float>(ScrollOffset);
			}
		}
		return Widget;
	}

	FUMGWidgetNode CreateEllipseWidget(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Shape.Geometry.IsEmpty()
			|| Node->Shape.InnerRadius > 0.0001f || FMath::Abs(Node->Shape.SweepAngle) < 359.9999f)
			return PathWidgetBuilder::CreatePathWidget(Node);
		const float Width = Node->Transform.Width.IsFixed() ? Node->Transform.Width.Value : 0.0f;
		const float Height = Node->Transform.Height.IsFixed() ? Node->Transform.Height.Value : 0.0f;
		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		const bool bHasGradientFill = HasGradientVisualFill(Node);
		const bool bHasStroke = Node->Visual.Stroke.HasStroke();

		if (Width > 0.0f
			&& Height > 0.0f
			&& ((PrimaryFill.IsColor() && PrimaryFill.bEnabled && !bHasStroke)
				|| bHasGradientFill))
		{
			return PathWidgetBuilder::CreatePathWidget(Node);
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
	case EDesignNodeType::Script:
		Result.Widget.Type = UMGWidgetTypes::CanvasPanel;
		return Result;

	case EDesignNodeType::Browser:
		{
			Result.Widget.Type = UMGWidgetTypes::WebBrowser;
			FUMGBrowserData& Browser = Result.Widget.Browser;
			const TSharedPtr<FJsonObject>& Properties = Node->Source().Pencil.Properties;
			if (Properties.IsValid())
			{
				Properties->TryGetStringField(TEXT("url"), Browser.InitialURL);
				Properties->TryGetStringField(TEXT("deviceId"), Browser.DeviceId);
				auto ReadNumber = [&Properties](const TCHAR* Key, TOptional<float>& Out)
				{
					double Value;
					if (Properties->TryGetNumberField(Key, Value) && FMath::IsFinite(Value)
						&& FMath::Abs(Value) <= MAX_flt) Out = static_cast<float>(Value);
				};
				ReadNumber(TEXT("zoom"), Browser.Zoom);
				ReadNumber(TEXT("scrollX"), Browser.ScrollX);
				ReadNumber(TEXT("scrollY"), Browser.ScrollY);
			}
			if (!Browser.DeviceId.IsEmpty())
				Result.Warnings.Add(FString::Printf(TEXT("Browser '%s': deviceId retained as source data; native device emulation is not generated."), *Node->Id));
			if (Browser.InitialURL.StartsWith(TEXT("/")) || Browser.InitialURL.StartsWith(TEXT("file:"))
				|| (Browser.InitialURL.Len() > 2 && Browser.InitialURL[1] == ':' && Browser.InitialURL[2] == '/'))
				Result.Warnings.Add(FString::Printf(TEXT("Browser '%s': local HTML keeps source-relative resources; relocation and cooked packaging are not generated."), *Node->Id));
			const float Zoom = Browser.Zoom.Get(1.0f);
			if (Zoom <= 0.0f)
				Result.Warnings.Add(FString::Printf(TEXT("Browser '%s': non-positive zoom retained as source data; viewport zoom is not generated."), *Node->Id));
			else if (Zoom != 1.0f)
			{
				FUMGWidgetNode Viewport = MoveTemp(Result.Widget);
				Viewport.Id = Node->Id + TEXT("__browser_viewport");
				Viewport.Name = (Node->Name.IsEmpty() ? Node->Id : Node->Name) + TEXT("_BrowserViewport");
				Viewport.Bounds.W = Node->Transform.Width.Value / Zoom;
				Viewport.Bounds.H = Node->Transform.Height.Value / Zoom;
				// Inverse stretch anchors keep the CSS viewport proportional to its live parent size.
				// Render scaling magnifies the page without entering WebBrowserViewport's layout scale.
				Viewport.Slot.Anchors.Max.X = Viewport.Slot.Anchors.Max.Y = 1.0f / Zoom;
				Viewport.Slot.Offsets.Right = Viewport.Slot.Offsets.Bottom = 0.0f;
				Viewport.Render.RenderTransformScale = FVector2D(Zoom);
				Viewport.Render.RenderTransformPivot = FVector2D::ZeroVector;
				Result.Widget = FUMGWidgetNode();
				Result.Widget.Type = UMGWidgetTypes::CanvasPanel;
				Result.Widget.Render.Clipping = TEXT("ClipToBounds");
				Result.Widget.Children.Add(MoveTemp(Viewport));
			}
			return Result;
		}

	case EDesignNodeType::Text:
		{
			Result.Widget = Design2UMGTextWidgetBuilder::CreateTextWidget(Node, Args.FontMappings);
			if (Design2UMGSourceMetadata::IsFigmaTextPathNode(Node))
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
			IconFontArgs.FontMappings = Args.FontMappings;
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
		Result.Widget = PathWidgetBuilder::CreatePathWidget(Node);
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
