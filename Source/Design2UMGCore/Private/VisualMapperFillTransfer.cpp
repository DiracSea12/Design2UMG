// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapperFillTransfer.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "ImageFillUtils.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"

namespace
{
	namespace GradientHelpers
	{

		const TCHAR* ToTypedGradientTypeString(const EDesignGradientType Type)
		{
			switch (Type)
			{
			case EDesignGradientType::Radial:
				return TEXT("Radial");
			case EDesignGradientType::Angular:
				return TEXT("Angular");
			case EDesignGradientType::Linear:
			default:
				return TEXT("Linear");
			}
		}
	}

	void AddTextureSizeHints(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Transform.Width.IsFixed() && Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.IsFixed() && Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
	}

	FUMGWidgetNode MakeMultiFillOverlayShell(const FUMGWidgetNode& Widget)
	{
		FUMGWidgetNode Overlay;
		Overlay.Type = UMGWidgetTypes::Overlay;
		Overlay.Id = Widget.Id;
		Overlay.Name = Widget.Name;
		Overlay.Bounds = Widget.Bounds;
		Overlay.Slot = Widget.Slot;
		return Overlay;
	}

	FUMGWidgetNode MakeMultiFillLayerShell(const FUMGWidgetNode& Widget, const int32 FillIndex)
	{
		FUMGWidgetNode Layer;
		const FString FillSuffix = Design2UMGGeneratedNameSuffixes::Fill(FillIndex);
		Layer.Id = Widget.Id + FillSuffix;
		Layer.Name = Widget.Name + FillSuffix;
		Layer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Layer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		return Layer;
	}

	void PrepareMultiFillContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Name += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot = FUMGSlotData();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

	bool HasMultiFillBorderPaintBeyondTransferredLayers(const FUMGVisualBrushData& Brush)
	{
		const bool bHasVisibleBrushColor = Brush.BrushColor.IsSet()
			&& !Brush.BrushColor.GetValue().Equals(FLinearColor::Transparent);
		return bHasVisibleBrushColor
			|| Brush.OutlineThickness.IsSet()
			|| Brush.Margin.IsSet()
			|| Brush.OutlineColor.IsSet()
			|| Brush.MaterialRef.IsSet()
			|| Brush.MaterialParams.IsSet()
			|| Brush.Shape.IsSet()
			|| Brush.Image.HasAny()
			|| Brush.Gradient.HasAny();
	}

	bool CanElideMultiFillBorder(
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid()
			&& Widget.Type == UMGWidgetTypes::Border
			&& Widget.Children.Num() <= 1
			&& !Node->Visual.Stroke.HasStroke()
			&& !Widget.HasSourceData()
			&& !Widget.WidgetStyle.HasAny()
			&& Widget.TextStyle.FontSize == 0.0f
			&& Widget.TextStyle.SourceFontSize == 0.0f
			&& !Widget.TextStyle.LetterSpacing.IsSet()
			&& !Widget.Text.HasAny()
			&& !HasMultiFillBorderPaintBeyondTransferredLayers(Widget.Brush)
			&& !Widget.Effect.HasAny()
			&& !Widget.Path.HasAny()
			&& !Widget.Shadow.HasAny()
			&& !Widget.Render.HasAny()
			&& !Widget.WidgetState.HasAny()
			&& !Widget.ScrollBox.HasAny();
	}

	bool CanElideEmptyMultiFillContent(const FUMGWidgetNode& Widget)
	{
		return Widget.Children.Num() == 0
			&& !Widget.HasSourceData()
			&& !Widget.WidgetStyle.HasAny()
			&& !HasMultiFillBorderPaintBeyondTransferredLayers(Widget.Brush)
			&& !Widget.Brush.DrawAs.IsSet()
			&& !Widget.Brush.CornerRadii.IsSet()
			&& Widget.TextStyle.FontSize == 0.0f
			&& Widget.TextStyle.SourceFontSize == 0.0f
			&& !Widget.TextStyle.LetterSpacing.IsSet()
			&& !Widget.Text.HasAny()
			&& !Widget.Effect.HasAny()
			&& !Widget.Path.HasAny()
			&& !Widget.Shadow.HasAny()
			&& !Widget.Render.HasAny()
			&& !Widget.WidgetState.HasAny()
			&& !Widget.ScrollBox.HasAny();
	}

	bool PrepareMultiFillContentForAppend(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		if (CanElideMultiFillBorder(Widget, Node))
		{
			if (Widget.Children.Num() == 0)
			{
				return false;
			}

			const FString ContentId = Widget.Id;
			const FString ContentName = Widget.Name;
			FUMGWidgetNode PromotedContent = MoveTemp(Widget.Children[0]);
			Widget = MoveTemp(PromotedContent);
			Widget.Id = ContentId;
			Widget.Name = ContentName;
		}
		else if (CanElideEmptyMultiFillContent(Widget))
		{
			return false;
		}

		PrepareMultiFillContentWidget(Widget);
		return true;
	}

	void ApplyMultiFillImageChildOpacity(
		FUMGWidgetNode& ImageChild,
		const FDesignFill& Fill,
		const FVisualMapperOptions& Options)
	{
		const bool bLuminosityFit = Fill.BlendMode.Equals(TEXT("LUMINOSITY"), ESearchCase::IgnoreCase);
		if (bLuminosityFit)
		{
			const float EffectiveOpacity = FMath::Min(Fill.Image.Opacity, Options.LuminosityFitOpacityCap);
			ImageChild.Render.RenderOpacity = EffectiveOpacity;
		}
		else if (Fill.Image.Opacity < 1.0f)
		{
			ImageChild.Render.RenderOpacity = Fill.Image.Opacity;
		}
	}

	bool TryBuildMultiFillLayer(
		FUMGWidgetNode& Layer,
		const FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FDesignFill& Fill,
		const int32 FillIndex,
		const FVisualMapperOptions& Options)
	{
		if (!Fill.bEnabled || Fill.Type == EDesignFillType::None)
		{
			return false;
		}

        if(Fill.Type==EDesignFillType::Shader && Fill.Shader.IsSet())
        {
            Layer.Type=UMGWidgetTypes::Image;
            Layer.Brush.Shader=Fill.Shader;
            Layer.Brush.Shader->bFillPaint = true;
            Layer.Brush.Shader->CaptureInsets = UMGRetainerCapture::FillInsets(*Node);
            Layer.Brush.Shader->Size=FVector2D(Node->Transform.Width.Value,Node->Transform.Height.Value);
            Layer.Path=Widget.Path;
            Layer.Brush.OutlineCornerRadius = Widget.Brush.OutlineCornerRadius;
            Layer.Brush.CornerRadii = Widget.Brush.CornerRadii;
            if(Layer.Path.Geometry.IsSet()) Layer.Path.FillColor=FLinearColor::White;
            AddTextureSizeHints(Layer,Node);
            return true;
        }
		if (Widget.Path.Geometry.IsSet())
		{
			Layer.Type = UMGWidgetTypes::Image;
			Layer.Path.Geometry = Widget.Path.Geometry;
			Layer.Path.ViewBox = Widget.Path.ViewBox;
			Layer.Path.SourcePaintGeometry = Widget.Path.SourcePaintGeometry;
			Layer.Path.FitGeometryToBounds = Widget.Path.FitGeometryToBounds;
			Layer.Path.CanvasSize = Widget.Path.CanvasSize;
			Layer.Path.FillRule = Widget.Path.FillRule;
			Layer.Path.FillColor = FLinearColor::White;
			if (Fill.Type == EDesignFillType::Color)
				Layer.Path.FillColor = FUMGValueParser::ParseColor(Fill.SolidColor);
			else if (Fill.Type == EDesignFillType::Gradient)
				VisualMapperFillTransfer::ApplyGradientBrushData(Layer.Brush, Fill.Gradient);
			else if (Fill.Type == EDesignFillType::Image && !Fill.Image.Url.IsEmpty())
			{
				ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
				Layer.Brush.Image.TexturePath = Fill.Image.Url;
				ApplyMultiFillImageChildOpacity(Layer, Fill, Options);
			}
			else return false;
			AddTextureSizeHints(Layer, Node);
			return true;
		}

		if (Fill.Type == EDesignFillType::Color)
		{
			Layer.Type = UMGWidgetTypes::Border;
			Layer.Brush.BrushColor = FUMGValueParser::ParseColor(Fill.SolidColor);
			return true;
		}

		if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
		{
			Layer.Type = UMGWidgetTypes::Image;
			VisualMapperFillTransfer::ApplyGradientBrushData(Layer.Brush, Fill.Gradient);
			AddTextureSizeHints(Layer, Node);
			return true;
		}

		if (Fill.Type != EDesignFillType::Image || Fill.Image.Url.IsEmpty())
		{
			return false;
		}

		const bool bSquareNode =
			Node->Transform.Width.Value > 0.0f
			&& Node->Transform.Height.Value > 0.0f
			&& FMath::IsNearlyEqual(Node->Transform.Width.Value, Node->Transform.Height.Value, 1.0f);
		if (Fill.Image.Mode == EDesignImageMode::Fit)
		{
			Layer.Type = UMGWidgetTypes::ScaleBox;
			Layer.WidgetStyle.Stretch = TEXT("ScaleToFit");
			Layer.WidgetStyle.StretchDirection = TEXT("Both");

			FUMGWidgetNode ImageChild;
			ImageChild.Type = UMGWidgetTypes::Image;
			const FString FillImageSuffix = Design2UMGGeneratedNameSuffixes::FillImage(FillIndex);
			ImageChild.Id = Widget.Id + FillImageSuffix;
			ImageChild.Name = Widget.Name + FillImageSuffix;
			ImageChild.Brush.Image.TexturePath = Fill.Image.Url;
			ApplyMultiFillImageChildOpacity(ImageChild, Fill, Options);
			if (bSquareNode)
			{
				AddTextureSizeHints(ImageChild, Node);
			}
			Layer.Children.Add(MoveTemp(ImageChild));
			return true;
		}

		Layer.Type = UMGWidgetTypes::Image;
		Layer.Brush.Image.TexturePath = Fill.Image.Url;
		ImageFillUtils::ApplyImageFillProperties(Layer, Fill.Image);
		AddTextureSizeHints(Layer, Node);
		return true;
	}
}

namespace VisualMapperFillTransfer
{
	void ApplyGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient)
	{
		Brush.Gradient.Type = GradientHelpers::ToTypedGradientTypeString(Gradient.GradientType);
		Brush.Gradient.Rotation = Gradient.Rotation;
		Brush.Gradient.Mesh = Gradient.Mesh;
		if (Gradient.StartPoint.IsSet())
		{
			Brush.Gradient.StartPoint = Gradient.StartPoint.GetValue();
		}
		if (Gradient.EndPoint.IsSet())
		{
			Brush.Gradient.EndPoint = Gradient.EndPoint.GetValue();
		}
		if (Gradient.Center.IsSet())
		{
			Brush.Gradient.Center = Gradient.Center.GetValue();
		}
		if (Gradient.Size.IsSet())
		{
			Brush.Gradient.Size = Gradient.Size.GetValue();
		}
		Brush.Gradient.Stops.Reset();
		for (const FDesignGradientStop& Stop : Gradient.Colors)
		{
			FUMGVisualBrushData::FGradientStopData& TypedStop = Brush.Gradient.Stops.AddDefaulted_GetRef();
			TypedStop.Color = FUMGValueParser::ParseColor(Stop.Color);
			TypedStop.Position = Stop.Position;
		}
	}
	void ApplyFillProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return;
		}
		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		if (!PrimaryFill.bEnabled)
		{
			return;
		}

		if (Node->HasMultiFill())
		{
			if ((Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
				&& PrimaryFill.Type == EDesignFillType::Color
				&& !Widget.Text.Color.IsSet())
			{
				Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
			}
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Color)
		{
			if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
			{
				if (!Widget.Text.Color.IsSet())
				{
					Widget.Text.Color = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
				}
				return;
			}

			// Rasterized path widgets bake the fill color into their texture, so a
			// brush tint would square the color; Widget.Path is set only on that
			// raster branch and is the ground truth. Node.Shape.Geometry is not
			// equivalent: Figma ellipses carry fillGeometry yet fall back to a
			// tint-only Border when stroked, while pen ellipses rasterize through
			// a side-effect-free clone whose geometry never reaches their node.
			if (!Widget.Path.Geometry.IsSet())
			{
				Widget.Brush.BrushColor = FUMGValueParser::ParseColor(PrimaryFill.SolidColor);
			}
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Gradient && PrimaryFill.Gradient.Colors.Num() > 0)
		{
			VisualMapperFillTransfer::ApplyGradientBrushData(Widget.Brush, PrimaryFill.Gradient);
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty())
		{
			ImageFillUtils::ApplyImageFillProperties(Widget, PrimaryFill.Image);
			AddTextureSizeHints(Widget, Node);
		}
	}

	FUMGWidgetNode WrapWithMultiFill(
		FUMGWidgetNode&& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& Options)
	{
		if (!Node.IsValid() || Node->Visual.Fills.Num() <= 1)
		{
			return MoveTemp(Widget);
		}

		FUMGWidgetNode Overlay = MakeMultiFillOverlayShell(Widget);
		Overlay.Shadow.BoxShadows = MoveTemp(Widget.Shadow.BoxShadows);
		for (int32 FillIndex = 0; FillIndex < Node->Visual.Fills.Num(); ++FillIndex)
		{
			const FDesignFill& Fill = Node->Visual.Fills[FillIndex];
			FUMGWidgetNode Layer = MakeMultiFillLayerShell(Widget, FillIndex);
			if (!TryBuildMultiFillLayer(Layer, Widget, Node, Fill, FillIndex, Options))
			{
				continue;
			}

			if (Node->Visual.CornerRadius.HasRadius())
			{
				VisualMapper::ApplyRoundedCorner(
					Layer,
					Node->Visual.CornerRadius,
					Node->Transform.Width.Value,
					Node->Transform.Height.Value);
			}

			Overlay.Children.Add(MoveTemp(Layer));
		}

		Overlay.Render = Widget.Render;
		Widget.Render = FUMGRenderStyleData();
		if (PrepareMultiFillContentForAppend(Widget, Node))
		{
			Overlay.Children.Add(MoveTemp(Widget));
		}
		return Overlay;
	}
}
