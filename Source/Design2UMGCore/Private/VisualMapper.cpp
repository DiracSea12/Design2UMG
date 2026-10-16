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

void VisualMapper::ApplyRenderTransformProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	ApplyRenderTransformPropertiesInternal(Widget, Node);
}

void VisualMapper::ApplyVisualProperties(FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
{
	VisualMapperFillTransfer::ApplyFillProperties(Widget, Node);
	ApplyRenderTransformPropertiesInternal(Widget, Node);

	ApplyRoundedCorner(Widget, Node->Visual.CornerRadius, Node->Transform.Width.Value, Node->Transform.Height.Value);
	ApplyStroke(Widget, ResolveStrokeCompositedOverFill(Node));

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
			if (Node->Type == EDesignNodeType::Text)
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
				Layer.Spread = Node->Type == EDesignNodeType::IconFont ? 0.0f : Effect.Shadow.Spread;
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
			const float MinDim = bHasResolvedSize ? FMath::Min(Width, Height) : 0.0f;
			if (MinDim > 0.0f && Radius.Uniform >= (MinDim * 0.5f - KINDA_SMALL_NUMBER))
			{
				// Treat large uniform radius as pill to match design tools that clamp radius to half-height.
				ExportRadius = UMGPropertyKeys::CapsuleRadiusSentinel;
			}
			else if (!bHasResolvedSize && Radius.Uniform >= UnresolvedSizePillRadiusThreshold)
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
