// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "DesignSchema.h"
#include "VisualMapperInternal.h"
#include "VisualMapperStrokeSupport.h"
#include "VisualMapperFillTransfer.h"
#include "DesignColorUtils.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "DesignJsonUtils.h"
#include "DesignSchemaSourceExtensions.h"
#include "DesignRoundedContour.h"
#include "DesignNodeSemantics.h"

namespace
{
	// CapsuleRadiusSentinel lives in UMGPropertyKeys.h (cross-module contract).
	// With no resolved size, a uniform radius at or above this threshold is
	// treated as a pill. Same provenance as the sentinel.
	constexpr float UnresolvedSizePillRadiusThreshold = 100.0f;

	void ApplyRenderTransformPropertiesInternal(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		if (Node->Transform.Opacity < 1.0f)
		{
			if (Widget.Render.RenderOpacity.IsSet())
			{
				// The image fill opacity has already been written; combine it with node-level opacity as a product.
				Widget.Render.RenderOpacity = Node->Transform.Opacity * Widget.Render.RenderOpacity.GetValue();
			}
			else
			{
				Widget.Render.RenderOpacity = Node->Transform.Opacity;
			}
		}

		if (!FMath::IsNearlyZero(Node->Transform.Rotation))
		{
			Widget.Render.RenderAngle = Node->Transform.Rotation;
		}

		if (Node->Transform.bFlipX || Node->Transform.bFlipY)
		{
			const float ScaleX = Node->Transform.bFlipX ? -1.0f : 1.0f;
			const float ScaleY = Node->Transform.bFlipY ? -1.0f : 1.0f;
			Widget.Render.RenderTransformScale = FVector2D(ScaleX, ScaleY);
		}

		if ((!FMath::IsNearlyZero(Node->Transform.Rotation) || Node->Transform.bFlipX || Node->Transform.bFlipY)
			&& !Node->Transform.TransformOrigin.Equals(FVector2D(0.5, 0.5)))
		{
			Widget.Render.RenderTransformPivot = Node->Transform.TransformOrigin;
		}

		if (Node->LayoutData.ExplicitClip.Get(false))
		{
			Widget.Render.Clipping = TEXT("ClipToBounds");
		}
	}

	// Composite a translucent inside stroke over its opaque fill for UMG RoundedBox.
	// Center/outside strokes lie outside that fill band and keep their own color.
	FDesignStroke ResolveStrokeCompositedOverFill(const TSharedPtr<FDesignNode>& Node)
	{
		FDesignStroke Stroke = Node->Visual.Stroke;
		if (Stroke.Align != EDesignStrokeAlign::Inside)
		{
			return Stroke;
		}
		if (!Stroke.Fill.IsSet() || Stroke.Fill->Type != EDesignFillType::Color || !Stroke.Fill->bEnabled)
		{
			return Stroke;
		}

		const FColor StrokeColor = DesignColorUtils::ParseHexColor(Stroke.Fill->SolidColor);
		if (StrokeColor.A >= 255)
		{
			return Stroke;
		}

		const FDesignFill& UnderlayFill = Node->Visual.TopmostFill();
		if (!UnderlayFill.bEnabled || UnderlayFill.Type != EDesignFillType::Color)
		{
			return Stroke;
		}
		const FColor FillColor = DesignColorUtils::ParseHexColor(UnderlayFill.SolidColor);
		if (FillColor.A < 255)
		{
			return Stroke;
		}

		const float StrokeAlpha = StrokeColor.A / 255.0f;
		const auto Mix = [StrokeAlpha](uint8 StrokeChannel, uint8 FillChannel)
		{
			return static_cast<uint8>(FMath::RoundToInt(StrokeChannel * StrokeAlpha + FillChannel * (1.0f - StrokeAlpha)));
		};
		const FColor Composited(
			Mix(StrokeColor.R, FillColor.R),
			Mix(StrokeColor.G, FillColor.G),
			Mix(StrokeColor.B, FillColor.B),
			255);
		Stroke.Fill->SolidColor = FString::Printf(
			TEXT("#%02X%02X%02XFF"), Composited.R, Composited.G, Composited.B);
		return Stroke;
	}
}

namespace VisualMapperInternal
{
void TransferWrapperRenderProperties(FUMGWidgetNode& Wrapper, FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	// Direct typed reads with descriptor-identical presence rules.
	if (!Wrapper.Render.Clipping.IsSet() && Widget.Render.Clipping.IsSet()
		&& !Widget.Render.Clipping.GetValue().IsEmpty())
	{
		Wrapper.Render.Clipping = Widget.Render.Clipping.GetValue();
	}
	if (!Wrapper.Render.RenderOpacity.IsSet() && Widget.Render.RenderOpacity.IsSet())
	{
		Wrapper.Render.RenderOpacity = Widget.Render.RenderOpacity.GetValue();
	}
	if (!Wrapper.Render.RenderAngle.IsSet() && Widget.Render.RenderAngle.IsSet())
	{
		Wrapper.Render.RenderAngle = Widget.Render.RenderAngle.GetValue();
	}
	if (!Wrapper.Render.RenderTransformScale.IsSet() && Widget.Render.RenderTransformScale.IsSet())
	{
		Wrapper.Render.RenderTransformScale = Widget.Render.RenderTransformScale.GetValue();
	}
	if (!Wrapper.Render.RenderTransformPivot.IsSet() && Widget.Render.RenderTransformPivot.IsSet())
	{
		Wrapper.Render.RenderTransformPivot = Widget.Render.RenderTransformPivot.GetValue();
	}
	if (!Wrapper.Render.RenderTranslation.IsSet() && Widget.Render.RenderTranslation.IsSet())
	{
		Wrapper.Render.RenderTranslation = Widget.Render.RenderTranslation.GetValue();
	}

	Widget.Render = FUMGRenderStyleData();

	if (!Wrapper.Render.RenderOpacity.IsSet() && Node->Transform.Opacity < 1.0f)
	{
		Wrapper.Render.RenderOpacity = Node->Transform.Opacity;
	}
}
}

namespace
{
	bool CrossesCornerArcs(const FBox2D& Paint, const FVector2D& Size, const FVector4& Radii)
	{
		for (int32 Corner = 0; Corner < 4; ++Corner) // TL, TR, BR, BL
		{
			const double Radius = FMath::Min<double>(Radii[Corner], 0.5 * FMath::Min(Size.X, Size.Y));
			if (Radius <= 0.0) continue;
			const bool bRight = Corner == 1 || Corner == 2, bBottom = Corner >= 2;
			// Fold the corner onto the top-left so the arc centre is (Radius, Radius).
			const double MinX = bRight ? Size.X - Paint.Max.X : Paint.Min.X, MaxX = bRight ? Size.X - Paint.Min.X : Paint.Max.X;
			const double MinY = bBottom ? Size.Y - Paint.Max.Y : Paint.Min.Y, MaxY = bBottom ? Size.Y - Paint.Min.Y : Paint.Max.Y;
			if (MaxX <= 0.0 || MaxY <= 0.0 || MinX >= Radius || MinY >= Radius) continue;
			const FVector2D Nearest(FMath::Max(MinX, 0.0), FMath::Max(MinY, 0.0));
			// Half a pixel of arc coverage is antialiasing, not visible overflow.
			if (FVector2D::Distance(Nearest, FVector2D(Radius)) > Radius + 0.5) return true;
		}
		return false;
	}

	// Rounded clipping is only visible where drawn content crosses a corner arc; transparent
	// containers are looked through so their painted descendants decide.
	bool ContentCrossesCornerArcs(const FDesignNode& Node, const FVector2D& Offset, const FVector2D& Size, const FVector4& Radii)
	{
		for (const TSharedPtr<FDesignNode>& Child : Node.Children)
		{
			if (!Child.IsValid() || !Child->Transform.bEnabled) continue;
			// Flow children clear their layout origin but keep the source placement they will be laid out at.
			const FVector2D Layout(Child->Transform.X, Child->Transform.Y);
			const FVector2D Origin = Offset + Child->Transform.SourceLayoutPosition.Get(Layout);
			if (!CrossesCornerArcs(VisualMapper::ChildPaintBounds(*Child).ShiftBy(Origin - Layout), Size, Radii)) continue;
			const bool bTransformed = Child->Transform.Rotation != 0.0f || Child->Transform.bFlipX || Child->Transform.bFlipY;
			if (bTransformed || Design2UMGCore::DesignNodeSemantics::HasRenderableVisual(Child)
				|| ContentCrossesCornerArcs(*Child, Origin, Size, Radii)) return true;
		}
		return false;
	}
}

bool VisualMapper::RequiresSourceContour(const FDesignNode& Node, bool bAnimatedSurface)
{
	return (Node.Type == EDesignNodeType::Frame || Node.Type == EDesignNodeType::Rectangle)
		&& Node.Source().Prototype.AnimationSource.IsValid() && Node.Visual.CornerRadius.HasRadius()
		&& (Node.Visual.CornerRadius.Smoothing != 0.0f || (Node.LayoutData.ExplicitClip.Get(false) && (bAnimatedSurface
			|| ContentCrossesCornerArcs(Node, FVector2D::ZeroVector, FVector2D(Node.Transform.Width.Value, Node.Transform.Height.Value),
				Node.Visual.CornerRadius.ToVector4()))));
}

bool VisualMapper::RequiresSourceContour(const FDesignNode& Node)
{
	return RequiresSourceContour(Node, Node.Source().Prototype.bAnimatedSurface);
}

bool VisualMapper::BuildSourceContour(const FDesignNode& Node, FUMGEffectData& Contour, FString& Error)
{
	FVector2D Canvas = Node.Transform.LayoutBoundsSize.Get(FVector2D(Node.Transform.Width.Value, Node.Transform.Height.Value));
	const auto Size = DesignJsonUtils::GetObject(Node.Source().Prototype.AnimationSource, TEXT("size"));
	double Width = 0, Height = 0;
	if (Size && Size->TryGetNumberField(TEXT("x"), Width) && Size->TryGetNumberField(TEXT("y"), Height)) Canvas = FVector2D(Width, Height);
	if (!FMath::IsFinite(Canvas.X) || !FMath::IsFinite(Canvas.Y) || Canvas.X <= 0 || Canvas.Y <= 0)
	{ Error = FString::Printf(TEXT("rounded source surface requires measured positive dimensions: '%s' (%s)"), *Node.Name, *Node.Id); return false; }
	const auto& Paths = Node.Shape.PaintGeometry.FillPaths;
	if (Paths.IsSet() && !Paths->IsEmpty()) Contour.GeometryMaskPaths = Paths.GetValue();
	else
	{
		// Fit dimensions already contain the shared layout/text measurement; source bounds retain only the authored box.
		if (Node.Transform.Width.IsFit()) Canvas.X = Node.Transform.Width.Value;
		if (Node.Transform.Height.IsFit()) Canvas.Y = Node.Transform.Height.Value;
		FDesignGeometryPath Primitive;
		if (!DesignRoundedContour::Build(Canvas, Node.Visual.CornerRadius, Primitive, Error)) return false;
		Contour.GeometryMaskPaths = TArray<FDesignGeometryPath>{MoveTemp(Primitive)};
	}
	if (Node.Visual.Stroke.HasStroke() && !Node.Shape.PaintGeometry.StrokePaths.IsSet())
	{
		const auto& Stroke = Node.Visual.Stroke;
		if (!Stroke.Fill.IsSet() || (Stroke.Fill->Type != EDesignFillType::Color && Stroke.Fill->Type != EDesignFillType::Gradient))
		{ Error = TEXT("parameter rounded strokes require an existing solid or gradient stroke paint"); return false; }
		if (!Stroke.Thickness.bUniform)
		{
			FDesignGeometryPath Region;
			if (Stroke.Align != EDesignStrokeAlign::Inside)
			{ Error = TEXT("independent primitive stroke edges require the supported inside alignment"); return false; }
			if (!DesignRoundedContour::BuildInsideStroke(Canvas, Node.Visual.CornerRadius, Stroke.Thickness, Region, Error)) return false;
		}
	}
	Contour.GeometryMaskSize = Canvas;
	Contour.bRetainRendering = true;
	Contour.EffectMaterial = UMGPropertyKeys::RetainerSourceContourEffectRef;
	Contour.TextureParameter = TEXT("Texture");
	return true;
}

void VisualMapper::ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	ApplyRenderTransformPropertiesInternal(Widget, Node);
}

void VisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	VisualMapperFillTransfer::ApplyFillProperties(Widget, Node);
	ApplyRenderTransformPropertiesInternal(Widget, Node);

	// Raster paths already own their shape corners and stroke paint.
	if (!Widget.Path.Geometry.IsSet())
	{
		ApplyRoundedCorner(Widget, Node->Visual.CornerRadius, Node->Transform.Width.Value, Node->Transform.Height.Value);
		ApplyStroke(Widget, ResolveStrokeCompositedOverFill(Node));
	}

	const FString GlyphPaintMode = Widget.Render.PaintBlendMode.Get(FString());
	// Native TextBlock shadows cannot blur or composite paint blend modes.
	const bool bTextShadowStack = Node->Type == EDesignNodeType::Text
		&& ((!GlyphPaintMode.IsEmpty() && GlyphPaintMode != TEXT("normal"))
			|| Node->Visual.Effects.ContainsByPredicate([](const FDesignEffect& Effect)
			{
				return Effect.bEnabled && Effect.Type == EDesignEffectType::Shadow
					&& ((!Effect.Shadow.bInner && Effect.Shadow.Blur > 0.0f)
						|| (!Effect.BlendMode.IsEmpty()
							&& VisualMapperFillTransfer::CanonicalPaintBlendMode(Effect.BlendMode) != TEXT("normal")));
			}));
	int32 ShadowCount = 0;
	bool bTextShadowAssigned = false;
	for (const FDesignEffect& Effect : Node->Visual.Effects)
	{
		if (!Effect.bEnabled)
			continue;

		switch (Effect.Type)
		{
		case EDesignEffectType::Blur:
			break;
		case EDesignEffectType::Shadow:
			if (Effect.Shadow.bInner)
			{
				break;
			}
			if (Node->Type == EDesignNodeType::Text && !bTextShadowStack)
			{
				// TextBlock supports only one shadow in UE5, so later shadows override earlier ones.
				if (bTextShadowAssigned)
				{
					UE_LOG(LogDesign2UMG, Verbose,
						TEXT("VisualMapper: text node '%s' has multiple drop shadows; the last enabled one wins (UMG TextBlock supports one)"),
						*Node->Name);
				}
				bTextShadowAssigned = true;
				Widget.Shadow.ShadowOffset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Widget.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? DesignColorUtils::DefaultShadowColorHex : Effect.Shadow.Color);
			}
			else
			{
				FUMGBoxShadowLayerData& Layer = Widget.Shadow.BoxShadows.AddDefaulted_GetRef();
				Layer.Offset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Layer.Blur = Effect.Shadow.Blur;
				// Glyph shadows use their painted alpha, not an expanded box.
				Layer.Spread = (Node->Type == EDesignNodeType::IconFont || Node->Type == EDesignNodeType::Text)
					? 0.0f : Effect.Shadow.Spread;
				Layer.BlendMode = VisualMapperFillTransfer::CanonicalPaintBlendMode(Effect.BlendMode);
				Layer.bShowShadowBehindNode = Effect.Shadow.bShowShadowBehindNode;
				Layer.Color = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? DesignColorUtils::DefaultShadowColorHex : Effect.Shadow.Color);
				++ShadowCount;
			}
			break;
		default:
			break;
		}
	}

	if (ShadowCount <= 0)
	{
		Widget.Shadow.BoxShadows.Reset();
	}
}

void VisualMapper::ApplyRoundedCorner(FUMGWidgetNode& Widget, const FDesignCornerRadius& Radius, float Width, float Height)
{
	if (Radius.bUniform)
	{
		if (Radius.Uniform > 0)
		{
			float ExportRadius = Radius.Uniform;
			const bool bHasResolvedSize = (Width > 0.0f && Height > 0.0f);
			if (bHasResolvedSize)
			{
				ExportRadius = FMath::Min(Radius.Uniform, FMath::Min(Width, Height) * 0.5f);
			}
			else if (Radius.Uniform >= UnresolvedSizePillRadiusThreshold)
			{
				// Height can be unresolved for fit-content nodes; large radii are typically intended as pill.
				ExportRadius = UMGPropertyKeys::CapsuleRadiusSentinel;
			}
			Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			Widget.Brush.OutlineCornerRadius = ExportRadius;
		}
	}
	else if (Radius.TopLeft > 0 || Radius.TopRight > 0 || Radius.BottomRight > 0 || Radius.BottomLeft > 0)
	{
		Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Widget.Brush.CornerRadii = FVector4(Radius.TopLeft, Radius.TopRight, Radius.BottomRight, Radius.BottomLeft);
	}
}

void VisualMapper::ApplyStroke(FUMGWidgetNode& Widget, const FDesignStroke& Stroke)
{
	// Non-solid paints use the exported stroke outline, not Slate's solid outline color.
	if (Stroke.Fill.IsSet() && Stroke.Fill->Type != EDesignFillType::Color) return;
	const FMargin HalfInsets = VisualMapperStrokeSupport::GetStrokeHalfInsets(Stroke.Thickness);
	float Thickness = Stroke.Thickness.bUniform ? Stroke.Thickness.Uniform
		: FMath::Max(FMath::Max(Stroke.Thickness.Top, Stroke.Thickness.Bottom),
			FMath::Max(Stroke.Thickness.Left, Stroke.Thickness.Right));

	if (Thickness <= 0)
		return;

	if (!Widget.Brush.DrawAs.IsSet())
	{
		// Slate draws OutlineSettings only for RoundedBox, including zero-radius
		// rectangles. Resource-backed brushes keep their existing rendering path.
		const bool bResourceBrush = Widget.Brush.MaterialRef.IsSet()
			|| Widget.Brush.Image.HasAny() || Widget.Brush.Gradient.HasAny();
		Widget.Brush.DrawAs = bResourceBrush
			? ESlateBrushDrawType::Box : ESlateBrushDrawType::RoundedBox;
	}

	Widget.Brush.OutlineThickness = Thickness;

	if (Stroke.Fill.IsSet() && Stroke.Fill->Type == EDesignFillType::Color)
	{
		Widget.Brush.OutlineColor = FUMGValueParser::ParseColor(Stroke.Fill->SolidColor);
	}

	// Stroke alignment compensation:
	// UMG outline is always centered on the edge. Adjust padding to simulate inside/outside.
	if (!Stroke.bIncludedInLayout)
		return;
	if (Stroke.Align == EDesignStrokeAlign::Inside)
	{
		// Inside: only inset sides that actually carry a stroke.
		Widget.WidgetStyle.StrokePadding = FMargin(HalfInsets.Left, HalfInsets.Top, HalfInsets.Right, HalfInsets.Bottom);
	}
	else if (Stroke.Align == EDesignStrokeAlign::Outside)
	{
		// Outside: only compensate on stroked sides, otherwise we shift content for no reason.
		Widget.WidgetStyle.StrokeMargin = FMargin(-HalfInsets.Left, -HalfInsets.Top, -HalfInsets.Right, -HalfInsets.Bottom);
	}
	// Center: default UMG behavior, no adjustment needed
}

FUMGWidgetNode VisualMapper::WrapWithMultiFill(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FVisualMapperOptions& Options)
{
	return VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(Widget), Node, Options);
}
