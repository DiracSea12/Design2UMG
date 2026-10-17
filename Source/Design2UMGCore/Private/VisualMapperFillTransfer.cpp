// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapperFillTransfer.h"

#include "Design2UMGGeneratedNameSuffixes.h"
#include "ImageFillUtils.h"
#include "Design2UMGLog.h"
#include "UMGValueParser.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGWidgetTypes.h"

namespace
{
	FString MakeRoundedRectanglePath(const FVector2D& Size, const FVector4& Radii, const float Inset)
	{
		const float TL = FMath::Max(0.0f, Radii.X - Inset), TR = FMath::Max(0.0f, Radii.Y - Inset);
		const float BR = FMath::Max(0.0f, Radii.Z - Inset), BL = FMath::Max(0.0f, Radii.W - Inset);
		const float Right = Size.X - Inset, Bottom = Size.Y - Inset;
		return FString::Printf(TEXT("M%f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f L%f %f A%f %f 0 0 1 %f %f Z"),
			Inset + TL, Inset, Right - TR, Inset, TR, TR, Right, Inset + TR,
			Right, Bottom - BR, BR, BR, Right - BR, Bottom,
			Inset + BL, Bottom, BL, BL, Inset, Bottom - BL,
			Inset, Inset + TL, TL, TL, Inset + TL, Inset);
	}

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

		void ApplyTypedGradientBrushData(FUMGVisualBrushData& Brush, const FDesignGradient& Gradient)
		{
			Brush.Gradient.Type = ToTypedGradientTypeString(Gradient.GradientType);
			Brush.Gradient.Rotation = Gradient.Rotation;
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

		if (Fill.Type == EDesignFillType::Color)
		{
			Layer.Type = UMGWidgetTypes::Border;
			Layer.Brush.BrushColor = FUMGValueParser::ParseColor(Fill.SolidColor);
			return true;
		}

		if (Fill.Type == EDesignFillType::Gradient && Fill.Gradient.Colors.Num() > 0)
		{
			Layer.Type = UMGWidgetTypes::Image;
			GradientHelpers::ApplyTypedGradientBrushData(Layer.Brush, Fill.Gradient);
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
			GradientHelpers::ApplyTypedGradientBrushData(Widget.Brush, PrimaryFill.Gradient);
			AddTextureSizeHints(Widget, Node);
			return;
		}

		if (PrimaryFill.Type == EDesignFillType::Image && !PrimaryFill.Image.Url.IsEmpty())
		{
			ImageFillUtils::ApplyImageFillProperties(Widget, PrimaryFill.Image);
			AddTextureSizeHints(Widget, Node);
		}
	}

	FUMGWidgetNode WrapWithGradientStroke(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node->Visual.Stroke.HasStroke() || !Node->Visual.Stroke.Fill.IsSet()
			|| Node->Visual.Stroke.Fill->Type != EDesignFillType::Gradient)
		{
			return MoveTemp(Widget);
		}
		TArray<FDesignGeometryPath> Paths;
		const bool bCanonicalOutline = Node->Visual.Stroke.Thickness.bUniform
			&& (Node->Type == EDesignNodeType::Rectangle || Node->Type == EDesignNodeType::Line);
		const float Thickness = Node->Visual.Stroke.Thickness.Uniform;
		FVector2D PaintSize(FMath::Max(1.0f, Node->Transform.Width.Value), FMath::Max(1.0f, Node->Transform.Height.Value));
		FVector2D PaintOffset = FVector2D::ZeroVector;
		const FVector4 RawRadii = Node->Visual.CornerRadius.ToVector4();
		const float RadiusLimit = FMath::Min(Node->Transform.Width.Value, Node->Transform.Height.Value) / 2.0f;
		const FVector4 Radii(FMath::Clamp(RawRadii.X, 0.0f, RadiusLimit), FMath::Clamp(RawRadii.Y, 0.0f, RadiusLimit),
			FMath::Clamp(RawRadii.Z, 0.0f, RadiusLimit), FMath::Clamp(RawRadii.W, 0.0f, RadiusLimit));

		if (bCanonicalOutline)
		{
			FString Geometry;
			const float Half = Thickness / 2.0f;
			if (Node->Type == EDesignNodeType::Line)
			{
				PaintSize.Y = Thickness;
				PaintOffset.Y = -Half;
				Geometry = FString::Printf(TEXT("M0 %f L%f %f"), Half, PaintSize.X, Half);
			}
			else
			{
				const float Expansion = Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside ? 0.0f
					: (Node->Visual.Stroke.Align == EDesignStrokeAlign::Center ? Half : Thickness);
				PaintOffset = FVector2D(-Expansion, -Expansion);
				PaintSize += FVector2D(2.0f * Expansion, 2.0f * Expansion);
				Geometry = MakeRoundedRectanglePath(PaintSize, Radii + FVector4(Expansion, Expansion, Expansion, Expansion), Half);
			}
			Paths = {{Geometry, TEXT("nonzero")}};
		}
		else Paths = Node->Shape.PaintGeometry.StrokePaths.Get(TArray<FDesignGeometryPath>());
		if (Paths.IsEmpty())
		{
			UE_LOG(LogDesign2UMG, Warning, TEXT("Gradient stroke '%s' requires exported stroke geometry."), *Node->Name);
			return MoveTemp(Widget);
		}
		const bool bStrokeOnly = Node->Children.IsEmpty() && Node->Visual.Fills.IsEmpty();
		// An inside stroke fits the native brush bounds; outside paint needs a separate layer.
		if (!bStrokeOnly && Widget.Type == UMGWidgetTypes::Border && bCanonicalOutline && Node->Type == EDesignNodeType::Rectangle
			&& Node->Visual.Stroke.Align == EDesignStrokeAlign::Inside)
		{
			Widget.Path.Geometry = MakeRoundedRectanglePath(PaintSize, Radii, 0.0f);
			Widget.Path.StrokeGeometry = Paths[0].Path;
			Widget.Path.FillRule = TEXT("nonzero");
			if (!Node->Visual.Fills.IsEmpty() && Node->Visual.TopmostFill().bEnabled)
				Widget.Path.FillColor = Widget.Brush.BrushColor.Get(FLinearColor::White);
			Widget.Path.StrokeColor = FLinearColor::White;
			Widget.Path.StrokeThickness = Thickness;
			FUMGVisualBrushData StrokeBrush;
			GradientHelpers::ApplyTypedGradientBrushData(StrokeBrush, Node->Visual.Stroke.Fill->Gradient);
			Widget.Path.StrokeGradient = MoveTemp(StrokeBrush.Gradient);
			auto FillGradient = MoveTemp(Widget.Brush.Gradient);
			auto FillImage = MoveTemp(Widget.Brush.Image);
			Widget.Brush = FUMGVisualBrushData();
			Widget.Brush.Gradient = MoveTemp(FillGradient);
			Widget.Brush.Image = MoveTemp(FillImage);
			Widget.Brush.DrawAs = ESlateBrushDrawType::Image;
			Widget.Brush.BrushColor = FLinearColor::White;
			Widget.WidgetStyle.WidthOverride = PaintSize.X;
			Widget.WidgetStyle.HeightOverride = PaintSize.Y;
			return MoveTemp(Widget);
		}
		FUMGWidgetNode Overlay = MakeMultiFillOverlayShell(Widget);
		Overlay.Render = MoveTemp(Widget.Render);
		Overlay.Shadow = MoveTemp(Widget.Shadow);
		Widget.Shadow = FUMGShadowData();
		Widget.Render = FUMGRenderStyleData();
		if (!bStrokeOnly)
		{
			PrepareMultiFillContentWidget(Widget);
			Overlay.Children.Add(MoveTemp(Widget));
		}
		for (int32 Index = 0; Index < Paths.Num(); ++Index)
		{
			const FDesignGeometryPath& Path = Paths[Index];
			FUMGWidgetNode Layer;
			Layer.Type = UMGWidgetTypes::Image;
			const FString Suffix = FString::Printf(TEXT("_stroke%d"), Index);
			Layer.Id = Overlay.Id + Suffix;
			Layer.Name = Overlay.Name + Suffix;
			Layer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
			Layer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
			Layer.Path.Geometry = Path.Path;
			Layer.Path.FillRule = Path.FillRule;
			if (bCanonicalOutline)
			{
				Layer.Path.StrokeColor = FLinearColor::White;
				Layer.Path.StrokeThickness = Thickness;
				Layer.Path.StrokeCap = Node->Visual.Stroke.Cap == EDesignStrokeCap::Circular ? TEXT("round")
					: (Node->Visual.Stroke.Cap == EDesignStrokeCap::Square ? TEXT("square") : TEXT("butt"));
			}
			else Layer.Path.FillColor = FLinearColor::White;
			Layer.Render.RenderTranslation = PaintOffset;
			GradientHelpers::ApplyTypedGradientBrushData(Layer.Brush, Node->Visual.Stroke.Fill->Gradient);
			Layer.WidgetStyle.WidthOverride = PaintSize.X;
			Layer.WidgetStyle.HeightOverride = PaintSize.Y;
			if (Layer.Brush.Gradient.StartPoint.IsSet())
				Layer.Brush.Gradient.StartPoint = (Layer.Brush.Gradient.StartPoint.GetValue() * FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value) - PaintOffset) / PaintSize;
			if (Layer.Brush.Gradient.EndPoint.IsSet())
				Layer.Brush.Gradient.EndPoint = (Layer.Brush.Gradient.EndPoint.GetValue() * FVector2D(Node->Transform.Width.Value, Node->Transform.Height.Value) - PaintOffset) / PaintSize;
			Overlay.Children.Add(MoveTemp(Layer));
		}
		if (bStrokeOnly && Overlay.Children.Num() == 1)
		{
			FUMGWidgetNode Layer = MoveTemp(Overlay.Children[0]);
			Layer.Id = Overlay.Id;
			Layer.Name = Overlay.Name;
			Layer.Bounds = Overlay.Bounds;
			Layer.Slot = Overlay.Slot;
			Layer.Render = MoveTemp(Overlay.Render);
			Layer.Render.RenderTranslation = Layer.Render.RenderTranslation.Get(FVector2D::ZeroVector) + PaintOffset;
			Layer.Shadow = MoveTemp(Overlay.Shadow);
			return Layer;
		}
		return Overlay;
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
