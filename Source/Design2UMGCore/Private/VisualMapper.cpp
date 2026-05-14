// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "VisualMapperStrokeSupport.h"
#include "ImageFillUtils.h"
#include "LayoutMapper.h"
#include "VisualMapperFillTransfer.h"
#include "Design2UMGLog.h"
#include "UMGPropertyKeys.h"
#include "UMGWidgetNodeAccess.h"
#include "UMGValueParser.h"

namespace
{
	// Material reference identifier used as an internal ProceduralShapeGenerator key, not an asset path.
	const TCHAR* RetainerForegroundBlurEffectRef = TEXT("d2u.retainer.foreground_blur");
	const TCHAR* RetainerInnerShadowEffectRef = TEXT("d2u.retainer.inner_shadow");
	const TCHAR* MatRef_RoundedRect = TEXT("roundedrect");

	FVector2D ComputeForegroundBlurUVStep(const TSharedPtr<FDesignNode>& Node, float BlurStrength)
	{
		if (!Node.IsValid())
		{
			return FVector2D(1.0f / 512.0f, 1.0f / 512.0f);
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		const float EffectiveBlur = FMath::Max(0.5f, BlurStrength) * 0.35f;
		return FVector2D(EffectiveBlur / Width, EffectiveBlur / Height);
	}

	FVector2D ComputeEffectUVOffset(const TSharedPtr<FDesignNode>& Node, float OffsetX, float OffsetY)
	{
		if (!Node.IsValid())
		{
			return FVector2D::ZeroVector;
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		return FVector2D(OffsetX / Width, OffsetY / Height);
	}

	bool HasOpaqueSolidFill(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid() || !Node->Visual.Fill.bEnabled || Node->Visual.Fill.Type != EDesignFillType::Color || Node->Visual.Fill.SolidColor.IsEmpty())
		{
			return false;
		}

		return FUMGValueParser::ParseColor(Node->Visual.Fill.SolidColor).A > KINDA_SMALL_NUMBER;
	}

	bool HasPlaceholderSurfaceChildren(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid())
		{
			return false;
		}

		for (const TSharedPtr<FDesignNode>& Child : Node->Children)
		{
			if (Child.IsValid() && Child->LayoutData.bSemanticPlaceholderSurface)
			{
				return true;
			}
		}

		return false;
	}

	bool HasVisualMapperPropertyPresence(const FUMGWidgetNode& Widget, const TCHAR* Key)
	{
		return Design2UMGWidgetNodeAccess::HasWidgetPropertyPresence(Widget, Key);
	}

	bool TryParseVisualMapperDrawAs(const FString& Value, ESlateBrushDrawType::Type& OutDrawAs)
	{
		static const TMap<FString, ESlateBrushDrawType::Type> DrawAsMap = {
			{ TEXT("NoDrawType"), ESlateBrushDrawType::NoDrawType },
			{ TEXT("Box"), ESlateBrushDrawType::Box },
			{ TEXT("Border"), ESlateBrushDrawType::Border },
			{ TEXT("Image"), ESlateBrushDrawType::Image },
			{ TEXT("RoundedBox"), ESlateBrushDrawType::RoundedBox },
		};

		if (const ESlateBrushDrawType::Type* Found = DrawAsMap.Find(Value))
		{
			OutDrawAs = *Found;
			return true;
		}

		return false;
	}

	void TransferBorderBackgroundProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FString StringValue;
		if (!Border.Brush.BrushColor.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::BrushColor, StringValue))
		{
			Border.Brush.BrushColor = FUMGValueParser::ParseColor(StringValue);
		}
		if (!Border.Brush.Image.TexturePath.IsSet())
		{
			if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::TexturePath, StringValue))
			{
				Border.Brush.Image.TexturePath = StringValue;
			}
		}
		if (!Border.Brush.MaterialRef.IsSet())
		{
			if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::MaterialRef, StringValue))
			{
				Border.Brush.MaterialRef = StringValue;
			}
		}
		if (!Border.Brush.MaterialParams.IsSet())
		{
			if (Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::MaterialParams, StringValue))
			{
				Border.Brush.MaterialParams = StringValue;
			}
		}

		bool bHasBackgroundProperty = false;
		if (Widget.WidgetStyle.WidthOverride.IsSet())
		{
			Border.WidgetStyle.WidthOverride = Widget.WidgetStyle.WidthOverride;
			Widget.WidgetStyle.WidthOverride.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.HeightOverride.IsSet())
		{
			Border.WidgetStyle.HeightOverride = Widget.WidgetStyle.HeightOverride;
			Widget.WidgetStyle.HeightOverride.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.ImageMode.IsSet())
		{
			Border.WidgetStyle.ImageMode = Widget.WidgetStyle.ImageMode;
			Widget.WidgetStyle.ImageMode.Reset();
			bHasBackgroundProperty = true;
		}
		if (Widget.WidgetStyle.ImageTransform.IsSet())
		{
			Border.WidgetStyle.ImageTransform = Widget.WidgetStyle.ImageTransform;
			Widget.WidgetStyle.ImageTransform.Reset();
			bHasBackgroundProperty = true;
		}

		bHasBackgroundProperty = bHasBackgroundProperty
			|| Border.Brush.BrushColor.IsSet()
			|| Border.Brush.Image.TexturePath.IsSet()
			|| Border.Brush.MaterialRef.IsSet();

		if (!bHasBackgroundProperty)
		{
			if (!Border.Brush.BrushColor.IsSet())
			{
				Border.Brush.BrushColor = FLinearColor::Transparent;
			}
		}
	}

	void TransferBorderStrokeProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FString StringValue;
		if (!Border.Brush.DrawAs.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::DrawAs, StringValue))
		{
			ESlateBrushDrawType::Type DrawAs = ESlateBrushDrawType::NoDrawType;
			if (TryParseVisualMapperDrawAs(StringValue, DrawAs))
			{
				Border.Brush.DrawAs = DrawAs;
			}
		}
		if (Border.Brush.OutlineCornerRadius < 0.0f)
		{
			float FloatValue = 0.0f;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::OutlineCornerRadius, FloatValue))
			{
				Border.Brush.OutlineCornerRadius = FloatValue;
			}
		}
		if (!Border.Brush.HasCornerRadii())
		{
			FVector4 VectorValue;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetVector4(Widget, UMGPropertyKeys::CornerRadii, VectorValue))
			{
				Border.Brush.CornerRadii = VectorValue;
			}
		}
		if (!Border.Brush.OutlineThickness.IsSet())
		{
			float FloatValue = 0.0f;
			if (Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::OutlineThickness, FloatValue))
			{
				Border.Brush.OutlineThickness = FloatValue;
			}
		}
		if (!Border.Brush.OutlineColor.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::OutlineColor, StringValue))
		{
			Border.Brush.OutlineColor = FUMGValueParser::ParseColor(StringValue);
		}

		if (Widget.WidgetStyle.StrokePadding.IsSet())
		{
			Border.WidgetStyle.StrokePadding = Widget.WidgetStyle.StrokePadding;
			Widget.WidgetStyle.StrokePadding.Reset();
		}
	}

	void ApplyOutsideStrokePaddingToBorder(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		FMargin Margin = FMargin(0.0f);
		bool bHasStrokeMargin = false;
		if (Widget.WidgetStyle.StrokeMargin.IsSet())
		{
			Margin = Widget.WidgetStyle.StrokeMargin.GetValue();
			bHasStrokeMargin = true;
		}
		else
		{
			bHasStrokeMargin = Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Widget, UMGPropertyKeys::StrokeMargin, Margin);
		}

		if (!bHasStrokeMargin)
		{
			return;
		}

		const FMargin Outset(
			FMath::Max(0.0f, -Margin.Left),
			FMath::Max(0.0f, -Margin.Top),
			FMath::Max(0.0f, -Margin.Right),
			FMath::Max(0.0f, -Margin.Bottom));
		if (Outset.Left > 0.0f || Outset.Top > 0.0f || Outset.Right > 0.0f || Outset.Bottom > 0.0f)
		{
			FMargin Combined = Outset;
			if (Border.WidgetStyle.StrokePadding.IsSet())
			{
				const FMargin ExistingPadding = Border.WidgetStyle.StrokePadding.GetValue();
				Combined.Left += ExistingPadding.Left;
				Combined.Top += ExistingPadding.Top;
				Combined.Right += ExistingPadding.Right;
				Combined.Bottom += ExistingPadding.Bottom;
			}
			else
			{
				FMargin ExistingPadding = FMargin(0.0f);
				if (Design2UMGWidgetNodeAccess::TryGetWidgetMargin(Border, UMGPropertyKeys::StrokePadding, ExistingPadding))
				{
					Combined.Left += ExistingPadding.Left;
					Combined.Top += ExistingPadding.Top;
					Combined.Right += ExistingPadding.Right;
					Combined.Bottom += ExistingPadding.Bottom;
				}
			}

			Border.WidgetStyle.StrokePadding = Combined;
		}

		Widget.WidgetStyle.StrokeMargin.Reset();
	}

	void TransferBorderRenderProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget, const TSharedPtr<FDesignNode>& Node)
	{
		FString StringValue;
		float FloatValue = 0.0f;
		FVector2D Vector2DValue = FVector2D::ZeroVector;

		if (!Border.Render.Clipping.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetString(Widget, UMGPropertyKeys::Clipping, StringValue)
			&& !StringValue.IsEmpty())
		{
			Border.Render.Clipping = StringValue;
		}
		if (!Border.Render.RenderOpacity.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::RenderOpacity, FloatValue))
		{
			Border.Render.RenderOpacity = FloatValue;
		}
		if (!Border.Render.RenderAngle.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetFloat(Widget, UMGPropertyKeys::RenderAngle, FloatValue))
		{
			Border.Render.RenderAngle = FloatValue;
		}
		if (!Border.Render.RenderTransformScale.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Widget, UMGPropertyKeys::RenderTransformScale, Vector2DValue))
		{
			Border.Render.RenderTransformScale = Vector2DValue;
		}
		if (!Border.Render.RenderTransformPivot.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Widget, UMGPropertyKeys::RenderTransformPivot, Vector2DValue))
		{
			Border.Render.RenderTransformPivot = Vector2DValue;
		}
		if (!Border.Render.RenderTranslation.IsSet()
			&& Design2UMGWidgetNodeAccess::TryGetWidgetVector2D(Widget, UMGPropertyKeys::RenderTranslation, Vector2DValue))
		{
			Border.Render.RenderTranslation = Vector2DValue;
		}

		Widget.Render = FUMGRenderStyleData();

		if (!Border.Render.RenderOpacity.IsSet() && Node->Transform.Opacity < 1.0f)
		{
			Border.Render.RenderOpacity = Node->Transform.Opacity;
		}
	}

	void TransferBorderBoxShadowProperties(FUMGWidgetNode& Border, FUMGWidgetNode& Widget)
	{
		if (Widget.Shadow.BoxShadows.Num() > 0)
		{
			Border.Shadow.BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
		}
	}

	void PrepareWrappedContentWidget(FUMGWidgetNode& Widget)
	{
		Widget.Id += TEXT("_inner");
		Widget.Name += TEXT("_inner");
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	}

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
	ApplyStroke(Widget, Node->Visual.Stroke);

	int32 ShadowCount = 0;
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
				Widget.Shadow.ShadowOffset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Widget.Shadow.ShadowColorAndOpacity = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
			}
			else
			{
				FUMGBoxShadowLayerData& Layer = Widget.Shadow.BoxShadows.AddDefaulted_GetRef();
				Layer.Offset = FVector2D(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
				Layer.Blur = Effect.Shadow.Blur;
				Layer.Spread = Effect.Shadow.Spread;
				Layer.Color = FUMGValueParser::ParseColor(
					Effect.Shadow.Color.IsEmpty() ? TEXT("#00000040") : Effect.Shadow.Color);
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
				ExportRadius = 999.0f;
			}
			else if (!bHasResolvedSize && Radius.Uniform >= 100.0f)
			{
				// Height can be unresolved for fit-content nodes; large radii are typically intended as pill.
				ExportRadius = 999.0f;
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

	if (!HasVisualMapperPropertyPresence(Widget, UMGPropertyKeys::DrawAs))
	{
		if (!Widget.Brush.DrawAs.IsSet())
		{
			Widget.Brush.DrawAs = ESlateBrushDrawType::Box;
		}
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

FUMGWidgetNode VisualMapper::WrapWithBorder(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Border;
	Border.Type = TEXT("Border");
	Border.Id = Widget.Id;
	Border.Name = Widget.Name;
	Border.Bounds = Widget.Bounds;

	// Transfer the visual brush to Border while keeping TextStyle on the original widget.
	Border.Brush = Widget.Brush;
	Widget.Brush = FUMGVisualBrushData();

	TransferBorderBackgroundProperties(Border, Widget);

	if (LayoutMapper::HasPadding(Node))
		LayoutMapper::ApplyPadding(Border, Node->LayoutData.ExplicitPadding.Get(FDesignPadding{}));

	TransferBorderStrokeProperties(Border, Widget);

	// Outside stroke: visual bounds from the parser already include the stroke expansion,
	// so do NOT expand Border.Bounds again. Only add StrokePadding so the centered UMG
	// outline doesn't eat into content space.
	ApplyOutsideStrokePaddingToBorder(Border, Widget);
	TransferBorderRenderProperties(Border, Widget, Node);
	TransferBorderBoxShadowProperties(Border, Widget);
	PrepareWrappedContentWidget(Widget);

	Border.Children.Add(MoveTemp(Widget));

	const bool bNeedsExplicitStroke =
		(VisualMapperStrokeSupport::NeedsExplicitSideStroke(Node)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& !HasOpaqueSolidFill(Node))
		|| (Node.IsValid()
			&& Node->Visual.Stroke.HasStroke()
			&& !Node->Visual.CornerRadius.HasRadius()
			&& VisualMapperStrokeSupport::HasUniformRectStroke(Node->Visual.Stroke)
			&& !HasPlaceholderSurfaceChildren(Node)
			&& VisualMapperStrokeSupport::HasAnyConvertedChildStrokeCoverage(Border));

	if (bNeedsExplicitStroke)
	{
		return VisualMapperStrokeSupport::WrapWithExplicitSideStroke(MoveTemp(Border), Node);
	}

	return Border;
}

FUMGWidgetNode VisualMapper::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
{
	FUMGWidgetNode Blur;
	Blur.Type = TEXT("BackgroundBlur");
	Blur.Id = Widget.Id + TEXT("_blur");
	Blur.Name = Widget.Name + TEXT("_blur");
	Blur.Bounds = Widget.Bounds;
	Blur.Slot = Widget.Slot;
	Blur.Effect.BlurStrength = BlurStrength;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
	Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

	Blur.Children.Add(MoveTemp(Widget));
	return Blur;
}

FUMGWidgetNode VisualMapper::WrapWithForegroundBlur(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, float BlurStrength)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = TEXT("RetainerBox");
	Retainer.Id = Widget.Id + TEXT("_fxblur");
	Retainer.Name = Widget.Name + TEXT("_fxblur");
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.EffectMaterial = RetainerForegroundBlurEffectRef;
	Retainer.Effect.TextureParameter = TEXT("Texture");

	const FVector2D BlurUVStep = ComputeForegroundBlurUVStep(Node, BlurStrength);
	Retainer.Effect.EffectBlurUVStep = BlurUVStep;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode VisualMapper::WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = TEXT("RetainerBox");
	Retainer.Id = Widget.Id + TEXT("_innershadow");
	Retainer.Name = Widget.Name + TEXT("_innershadow");
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.EffectMaterial = RetainerInnerShadowEffectRef;
	Retainer.Effect.TextureParameter = TEXT("Texture");
	Retainer.Effect.EffectColor = FUMGValueParser::ParseColor(
		Shadow.Color.IsEmpty() ? TEXT("#00000040") : Shadow.Color);
	Retainer.Effect.EffectOffset = ComputeEffectUVOffset(Node, Shadow.OffsetX, Shadow.OffsetY);
	Retainer.Effect.EffectBlurUVStep = ComputeForegroundBlurUVStep(Node, Shadow.Blur);

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode VisualMapper::WrapWithBoxShadow(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FUMGBoxShadowLayerData& ShadowLayer,
	int32 ShadowIndex)
{
	const float OffX = ShadowLayer.Offset.X;
	const float OffY = ShadowLayer.Offset.Y;
	const float Blur = ShadowLayer.Blur;
	const float Spread = ShadowLayer.Spread;
	const FString ShadowColor = Design2UMGWidgetNodeAccess::SerializeWidgetColor(ShadowLayer.Color);

	// Overlay contains the shadow Image and the original widget; Fill alignment works on Overlay children.
	FUMGWidgetNode Wrapper;
	Wrapper.Type = TEXT("Overlay");
	Wrapper.Id = Widget.Id;
	Wrapper.Name = Widget.Name;
	Wrapper.Bounds = Widget.Bounds;
	Wrapper.Slot = Widget.Slot;

	// Shadow Image node.
	const float PositiveSpread = FMath::Max(0.0f, Spread);
	const float NegativeSpreadInset = FMath::Max(0.0f, -Spread);
	const float Extent = Blur + PositiveSpread;
	const float ShapeWidth = FMath::Max(1.0f, Node->Transform.Width.Value - NegativeSpreadInset * 2.0f);
	const float ShapeHeight = FMath::Max(1.0f, Node->Transform.Height.Value - NegativeSpreadInset * 2.0f);
	FUMGWidgetNode Shadow;
	Shadow.Type = TEXT("Image");
	FString ShadowSuffix = FString::Printf(TEXT("_shadow%d"), ShadowIndex);
	Shadow.Id = Widget.Id + ShadowSuffix;
	Shadow.Name = Widget.Name + ShadowSuffix;
	Shadow.Brush.BrushColor = ShadowLayer.Color;

	// Shadow texture parameters.
	const int32 TexW = FMath::Max(1, FMath::RoundToInt(ShapeWidth + Extent * 2.0f));
	const int32 TexH = FMath::Max(1, FMath::RoundToInt(ShapeHeight + Extent * 2.0f));
	Shadow.WidgetStyle.WidthOverride = TexW;
	Shadow.WidgetStyle.HeightOverride = TexH;
	Shadow.Brush.MaterialRef = (Blur > 0.0f) ? TEXT("boxshadow") : MatRef_RoundedRect;
	Shadow.Shadow.ShadowBlur = Blur;

	const float HalfMin = FMath::Min(TexW, TexH) * 0.5f;
	if (Node->Visual.CornerRadius.bUniform)
	{
		const float NormalizedRadius = (HalfMin > 0.0f) ? (Node->Visual.CornerRadius.Uniform + Extent) / HalfMin : 0.0f;
		Shadow.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"CornerRadius\":%.3f}"),
			*ShadowColor,
			NormalizedRadius);
	}
	else
	{
		auto Norm = [&](float RadiusPx)
		{
			return (HalfMin > 0.0f) ? (RadiusPx + Extent) / HalfMin : 0.0f;
		};
		Shadow.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"CornerRadii\":[%.3f,%.3f,%.3f,%.3f]}"),
			*ShadowColor,
			Norm(Node->Visual.CornerRadius.TopLeft), Norm(Node->Visual.CornerRadius.TopRight),
			Norm(Node->Visual.CornerRadius.BottomRight), Norm(Node->Visual.CornerRadius.BottomLeft));
	}

	// Shadow offset positioning.
	Shadow.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
	Shadow.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
	const float ShadowMarginLeft = OffX - Extent + NegativeSpreadInset;
	const float ShadowMarginTop = OffY - Extent + NegativeSpreadInset;
	Shadow.Render.RenderTranslation = FVector2D(ShadowMarginLeft, ShadowMarginTop);

	Wrapper.Children.Add(MoveTemp(Shadow));

	// Place the original widget above the shadow.
	Widget.Id = Widget.Id + TEXT("_content");
	Widget.Name = Widget.Name + TEXT("_content");
	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
	Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

	Wrapper.Children.Add(MoveTemp(Widget));
	return Wrapper;
}

FUMGWidgetNode VisualMapper::WrapWithMultiFill(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const FVisualMapperOptions& Options)
{
	return VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(Widget), Node, Options);
}


