// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "VisualMapper.h"
#include "VisualMapperFillTransfer.h"
#include "UMGRetainerCapture.h"
#include "DesignSchema.h"
#include "VisualMapperInternal.h"
#include "VisualMapperStrokeSupport.h"
#include "DesignColorUtils.h"
#include "UMGPropertyKeys.h"
#include "UMGValueParser.h"
#include "UMGWidgetTypes.h"
#include "Design2UMGGeneratedNameSuffixes.h"

#include <cmath>

using VisualMapperInternal::TransferWrapperRenderProperties;

namespace
{
	// Material reference identifier used as an internal ProceduralShapeGenerator key, not an asset path.
	const TCHAR* MatRef_RoundedRect = TEXT("roundedrect");

	// UV step for foreground blur when node size is unresolved.
	constexpr float InvalidNodeBlurUVStepComponent = 1.0f / 512.0f;
	// Foreground-blur strength floor and strength-to-UV-step scale.
	constexpr float BlurStrengthToUVStepScale = 0.35f;

	// Bounds relative to the node's layout origin, before its own render transform.
    FBox2D IncludeOuterShadows(const FDesignNode& Node, const FBox2D& Content)
    {
        FBox2D Paint = Content;
        for (const FDesignEffect& Effect : Node.Visual.Effects)
        {
            if (!Effect.bEnabled || Effect.Type != EDesignEffectType::Shadow || Effect.Shadow.bInner) continue;
            const double Radius = Effect.Shadow.Blur * 1.5 + FMath::Max(0.0f, Effect.Shadow.Spread);
            const FVector2D Offset(Effect.Shadow.OffsetX, Effect.Shadow.OffsetY);
            Paint += FBox2D(Content.Min + Offset - FVector2D(Radius), Content.Max + Offset + FVector2D(Radius));
        }
        return Paint;
    }

	FBox2D SubtreePaintBounds(const FDesignNode& Node, bool bIncludeOwnShadows)
	{
		const FVector2D Size(Node.Transform.Width.Value, Node.Transform.Height.Value);
		FBox2D Content(FVector2D::ZeroVector, Size);
		if (Node.Visual.Stroke.Align != EDesignStrokeAlign::Inside)
		{
			FMargin Stroke = VisualMapperStrokeSupport::GetStrokeHalfInsets(Node.Visual.Stroke.Thickness);
			if (Node.Visual.Stroke.Align == EDesignStrokeAlign::Outside) Stroke = Stroke * 2.0f;
			Content += FBox2D(FVector2D(-Stroke.Left, -Stroke.Top), Size + FVector2D(Stroke.Right, Stroke.Bottom));
		}
		if (!Node.LayoutData.ExplicitClip.Get(false))
		{
			for (const TSharedPtr<FDesignNode>& Child : Node.Children)
			{
				if (!Child.IsValid() || !Child->Transform.bEnabled) continue;
				const FBox2D ChildPaint = SubtreePaintBounds(*Child, true);
				const FVector2D ChildSize(Child->Transform.Width.Value, Child->Transform.Height.Value);
				const FVector2D Pivot = ChildSize * Child->Transform.TransformOrigin;
				FBox2D Transformed(ForceInit);
				for (int32 Corner = 0; Corner < 4; ++Corner)
				{
					FVector2D Point((Corner & 1) ? ChildPaint.Max.X : ChildPaint.Min.X,
						(Corner & 2) ? ChildPaint.Max.Y : ChildPaint.Min.Y);
					Point -= Pivot;
					if (Child->Transform.bFlipX) Point.X = -Point.X;
					if (Child->Transform.bFlipY) Point.Y = -Point.Y;
					Transformed += Point.GetRotated(Child->Transform.Rotation) + Pivot;
				}
				Content += FBox2D(Transformed.Min + FVector2D(Child->Transform.X, Child->Transform.Y),
					Transformed.Max + FVector2D(Child->Transform.X, Child->Transform.Y));
				if (Node.LayoutData.Layout != EDesignLayout::None && !Child->LayoutData.bAbsoluteLayoutPositioning)
				{
					// Flow placement is resolved by UMG. Reserve each child's paint
					// outsets at either edge instead of assuming its source position.
					Content += FBox2D(FVector2D(FMath::Min(0.0, Transformed.Min.X), FMath::Min(0.0, Transformed.Min.Y)),
						Size + FVector2D(FMath::Max(0.0, Transformed.Max.X - ChildSize.X), FMath::Max(0.0, Transformed.Max.Y - ChildSize.Y)));
				}
			}
		}
		return bIncludeOwnShadows ? IncludeOuterShadows(Node, Content) : Content;
	}

	FVector2D ComputeForegroundBlurUVStep(const TSharedPtr<FDesignNode>& Node, float BlurStrength)
	{
		if (!Node.IsValid())
		{
			return FVector2D(InvalidNodeBlurUVStepComponent, InvalidNodeBlurUVStepComponent);
		}

		const float Width = FMath::Max(1.0f, Node->Transform.Width.Value);
		const float Height = FMath::Max(1.0f, Node->Transform.Height.Value);
		const float EffectiveBlur = FMath::Max(0.0f, BlurStrength) * BlurStrengthToUVStepScale;
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

	int32 QuantizePositivePixelExtent(float Value)
	{
		if (!FMath::IsFinite(Value) || Value <= 0.0f)
		{
			return 1;
		}

		constexpr int32 MaxQuantizableInt = TNumericLimits<int32>::Max() - 64;
		constexpr float MaxQuantizableIntAsFloat = static_cast<float>(MaxQuantizableInt);
		if (Value >= MaxQuantizableIntAsFloat)
		{
			return MaxQuantizableInt;
		}

		return FMath::RoundToInt(Value);
	}

	float QuantizeShadowParameter(float Value)
	{
		return static_cast<float>(std::nearbyint(static_cast<double>(Value) * 1000.0) / 1000.0);
	}

	float NormalizeShadowRadius(float Value)
	{
		// Preserve the existing raster precision and percentage/fraction convention.
		Value = QuantizeShadowParameter(Value);
		return Value > 1.0f ? Value / 100.0f : Value;
	}

	FUMGWidgetNode BuildBoxShadowLayerImage(
		const TSharedPtr<FDesignNode>& Node,
		const FUMGBoxShadowLayerData& ShadowLayer,
		int32 ShadowIndex,
		const FString& SourceId,
		const FString& SourceName)
	{
		const float OffX = ShadowLayer.Offset.X;
		const float OffY = ShadowLayer.Offset.Y;
		const float Blur = ShadowLayer.Blur;
		const float Spread = ShadowLayer.Spread;
		FUMGVisualBrushData::FShapeData Shape;
		Shape.Color = FLinearColor::FromSRGBColor(ShadowLayer.Color.ToFColor(true));

		const float PositiveSpread = FMath::Max(0.0f, Spread);
		const float NegativeSpreadInset = FMath::Max(0.0f, -Spread);
		// Fixed and fit sizes provide an extent for exact shadow geometry.
		// Fill sizes resolve at runtime, so their shadow tracks the wrapper without BlurPadding.
		auto HasUsableExtent = [](const FDesignSize& Size)
		{
			return !Size.IsFill() && Size.Value > 0.0f;
		};
		const bool bDeclaredShape = HasUsableExtent(Node->Transform.Width) && HasUsableExtent(Node->Transform.Height);
		FUMGWidgetNode Shadow;
		Shadow.Type = UMGWidgetTypes::Image;
		const FString ShadowSuffix = Design2UMGGeneratedNameSuffixes::Shadow(ShadowIndex);
		Shadow.Id = SourceId + ShadowSuffix;
		Shadow.Name = SourceName + ShadowSuffix;
		// The procedural texture already contains the shadow colour and alpha.
		// Keep its image tint neutral when that texture is persisted and rebound.
		Shadow.Brush.BrushColor = FLinearColor::White;
		Shadow.Brush.MaterialRef = Node->Type == EDesignNodeType::Ellipse
			? TEXT("ellipseshadow") : ((Blur > 0.0f) ? TEXT("boxshadow") : MatRef_RoundedRect);
		Shadow.Shadow.ShadowBlur = Blur;
    if (!ShadowLayer.BlendMode.IsEmpty() && ShadowLayer.BlendMode != TEXT("normal"))
        Shadow.Render.PaintBlendMode = ShadowLayer.BlendMode;

		float RadiusHalfMin;
		float RadiusExpansion;
		if (bDeclaredShape)
		{
			// The 3-sigma falloff needs 1.5 * Blur pixels on each side. Spread changes
			// the shape extent; the blur padding changes only the texture extent.
			const float BlurPaddingExtent = Blur * 1.5f;
			Shape.BlurPadding = QuantizeShadowParameter(BlurPaddingExtent);
			const float ShapeWidth = FMath::Max(1.0f, Node->Transform.Width.Value - NegativeSpreadInset * 2.0f + PositiveSpread * 2.0f);
			const float ShapeHeight = FMath::Max(1.0f, Node->Transform.Height.Value - NegativeSpreadInset * 2.0f + PositiveSpread * 2.0f);
			Shadow.WidgetStyle.WidthOverride = QuantizePositivePixelExtent(ShapeWidth + BlurPaddingExtent * 2.0f);
			Shadow.WidgetStyle.HeightOverride = QuantizePositivePixelExtent(ShapeHeight + BlurPaddingExtent * 2.0f);
			RadiusHalfMin = FMath::Min(ShapeWidth, ShapeHeight) * 0.5f;
			RadiusExpansion = PositiveSpread;
			// Unlike centered children, fill children with negative padding can
			// extend past SOverlay's parent rect without being clamped.
			Shadow.Slot.Padding = FMargin(-BlurPaddingExtent);
			Shadow.Render.RenderTranslation = FVector2D(OffX, OffY);
		}
		else
		{
			// Fill-sized shapes keep stretch-to-wrapper semantics: the falloff
			// stays inside the texture, and translation compensates its extent.
			const float ShadowTextureExtent = Blur + PositiveSpread;
			const float ShapeWidth = FMath::Max(1.0f, Node->Transform.Width.Value - NegativeSpreadInset * 2.0f);
			const float ShapeHeight = FMath::Max(1.0f, Node->Transform.Height.Value - NegativeSpreadInset * 2.0f);
			const int32 TexW = QuantizePositivePixelExtent(ShapeWidth + ShadowTextureExtent * 2.0f);
			const int32 TexH = QuantizePositivePixelExtent(ShapeHeight + ShadowTextureExtent * 2.0f);
			Shadow.WidgetStyle.WidthOverride = TexW;
			Shadow.WidgetStyle.HeightOverride = TexH;
			RadiusHalfMin = FMath::Min(TexW, TexH) * 0.5f;
			RadiusExpansion = ShadowTextureExtent;
			Shadow.Render.RenderTranslation = FVector2D(
				OffX - ShadowTextureExtent + NegativeSpreadInset,
				OffY - ShadowTextureExtent + NegativeSpreadInset);
		}
		const auto ResolveRadius = [&](float SourceRadius)
		{
			return NormalizeShadowRadius(RadiusHalfMin > 0.0f ? (SourceRadius + RadiusExpansion) / RadiusHalfMin : 0.0f);
		};
		if (Node->Visual.CornerRadius.bUniform)
		{
			Shape.CornerRadius = ResolveRadius(Node->Visual.CornerRadius.Uniform);
		}
		else
		{
			Shape.CornerRadii = FVector4(
				ResolveRadius(Node->Visual.CornerRadius.TopLeft), ResolveRadius(Node->Visual.CornerRadius.TopRight),
				ResolveRadius(Node->Visual.CornerRadius.BottomRight), ResolveRadius(Node->Visual.CornerRadius.BottomLeft));
		}
		Shadow.Brush.Shape = MoveTemp(Shape);
		Shadow.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Shadow.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		return Shadow;
	}
}

FUMGWidgetNode VisualMapper::WrapWithShaderEffect(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShaderData& Shader)
{
    const FVector2D Size(Node->Transform.Width.Value,Node->Transform.Height.Value);
    const FBox2D Paint=SubtreePaintBounds(*Node,true);
    const FMargin Gutter(FMath::CeilToFloat(FMath::Max(0.0,-Paint.Min.X)),
        FMath::CeilToFloat(FMath::Max(0.0,-Paint.Min.Y)),
        FMath::CeilToFloat(FMath::Max(0.0,Paint.Max.X-Size.X)),
        FMath::CeilToFloat(FMath::Max(0.0,Paint.Max.Y-Size.Y)));
    FUMGWidgetNode Retainer;
    Retainer.Type=UMGWidgetTypes::RetainerBox;
    Retainer.Id=Widget.Id+TEXT("_shaderEffect");Retainer.Name=Widget.Name+TEXT("_shaderEffect");
    Retainer.Bounds=Widget.Bounds;Retainer.Slot=Widget.Slot;
    Retainer.Effect.Shader=Shader;Retainer.Effect.Shader->Size=Size;
    Retainer.Effect.Shader->bFillPaint = false;
    Retainer.Effect.Shader->CaptureInsets=FVector4(Gutter.Left,Gutter.Top,Gutter.Right,Gutter.Bottom);
    Retainer.Effect.bRetainRendering=true;
    // Node clipping applies to captured content. Its transform/opacity apply after the shader.
    const auto ContentClip=Widget.Render.Clipping;
    Retainer.Render=MoveTemp(Widget.Render);Widget.Render=FUMGRenderStyleData();
    Retainer.Render.Clipping.Reset();Widget.Render.Clipping=ContentClip;
    Widget.Bounds=FDesignPixelBounds();Widget.Slot=FUMGSlotData();
    Widget.Slot.HorizontalAlignment=EUMGSlotHorizontalAlignment::Fill;
    Widget.Slot.VerticalAlignment=EUMGSlotVerticalAlignment::Fill;
    Retainer.Children.Add(MoveTemp(Widget));
    return UMGRetainerCapture::Wrap(MoveTemp(Retainer),Size,Gutter);
}

FUMGWidgetNode VisualMapper::WrapWithBackgroundBlur(FUMGWidgetNode&& Widget, float BlurStrength)
{
	FUMGWidgetNode Blur;
	Blur.Type = UMGWidgetTypes::BackgroundBlur;
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
	Retainer.Type = UMGWidgetTypes::RetainerBox;
	Retainer.Id = Widget.Id + Design2UMGGeneratedNameSuffixes::FxBlur();
	Retainer.Name = Widget.Name + Design2UMGGeneratedNameSuffixes::FxBlur();
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.EffectMaterial = UMGPropertyKeys::RetainerForegroundBlurEffectRef;
	Retainer.Effect.TextureParameter = TEXT("Texture");

	const FVector2D BlurUVStep = ComputeForegroundBlurUVStep(Node, BlurStrength);
	Retainer.Effect.EffectBlurUVStep = BlurUVStep;

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode VisualMapper::WrapWithGeometryMask(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = UMGWidgetTypes::RetainerBox;
	Retainer.Id = Widget.Id + Design2UMGGeneratedNameSuffixes::GeometryMask();
	Retainer.Name = Widget.Name + Design2UMGGeneratedNameSuffixes::GeometryMask();
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.EffectMaterial = UMGPropertyKeys::RetainerGeometryMaskEffectRef;
	Retainer.Effect.TextureParameter = TEXT("Texture");
	Retainer.Effect.GeometryMaskShape = Node->Visual.GeometryMask.Shape == EDesignGeometryMaskShape::Ellipse
		? EUMGGeometryMaskShape::Ellipse
		: EUMGGeometryMaskShape::RoundedRectangle;
	Retainer.Effect.GeometryMaskSize = FVector2D(
		FMath::Max(1.0f, Node->Transform.Width.Value),
		FMath::Max(1.0f, Node->Transform.Height.Value));
	Retainer.Effect.GeometryMaskCornerRadii = Node->Visual.GeometryMask.CornerRadius.ToVector4();

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode VisualMapper::WrapWithInnerShadow(FUMGWidgetNode&& Widget, const TSharedPtr<FDesignNode>& Node, const FDesignShadow& Shadow)
{
	FUMGWidgetNode Retainer;
	Retainer.Type = UMGWidgetTypes::RetainerBox;
	Retainer.Id = Widget.Id + Design2UMGGeneratedNameSuffixes::InnerShadow();
	Retainer.Name = Widget.Name + Design2UMGGeneratedNameSuffixes::InnerShadow();
	Retainer.Bounds = Widget.Bounds;
	Retainer.Slot = Widget.Slot;
	Retainer.Effect.bRetainRendering = true;
	Retainer.Effect.EffectMaterial = UMGPropertyKeys::RetainerInnerShadowEffectRef;
	Retainer.Effect.TextureParameter = TEXT("Texture");
	Retainer.Effect.EffectColor = FUMGValueParser::ParseColor(
		Shadow.Color.IsEmpty() ? DesignColorUtils::DefaultShadowColorHex : Shadow.Color);
	Retainer.Effect.EffectOffset = ComputeEffectUVOffset(Node, Shadow.OffsetX, Shadow.OffsetY);
	Retainer.Effect.EffectBlurUVStep = ComputeForegroundBlurUVStep(Node, Shadow.Blur);
    if (!Shadow.BlendMode.IsEmpty() && VisualMapperFillTransfer::CanonicalPaintBlendMode(Shadow.BlendMode) != TEXT("normal"))
        Retainer.Effect.InnerShadowBlendMode = VisualMapperFillTransfer::CanonicalPaintBlendMode(Shadow.BlendMode);

	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Retainer.Children.Add(MoveTemp(Widget));
	return Retainer;
}

FUMGWidgetNode VisualMapper::WrapWithBoxShadow(
	FUMGWidgetNode&& Widget,
	const TSharedPtr<FDesignNode>& Node,
	const TArray<FUMGBoxShadowLayerData>& ShadowLayers,
    const FDesignShadow* InnerShadow)
{
    TOptional<FString> GlyphPaintBlendMode;
    if (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
    {
        GlyphPaintBlendMode = Widget.Render.PaintBlendMode;
        Widget.Render.PaintBlendMode.Reset();
    }
	FUMGWidgetNode Wrapper;
	// Keep the wrapper in the parent's established layout flow. It owns the
	// source render properties so the shadows and content share one render space.
	Wrapper.Type = UMGWidgetTypes::Overlay;
	Wrapper.Id = Widget.Id;
	Wrapper.Name = Widget.Name;
	Wrapper.Bounds = Widget.Bounds;
	Wrapper.Slot = Widget.Slot;
	TransferWrapperRenderProperties(Wrapper, Widget, Node);

	if (Node->Type == EDesignNodeType::IconFont
		|| Node->Type == EDesignNodeType::Text
		|| (!Node->Children.IsEmpty() && Node->Visual.Fills.IsEmpty()))
	{
		FMargin Gutter;
		for (const FUMGBoxShadowLayerData& Shadow : ShadowLayers)
		{
			const float Radius = FMath::CeilToFloat(Shadow.Blur * 1.5f + FMath::Abs(Shadow.Spread)) + 1.0f;
			Gutter.Left = FMath::Max(Gutter.Left, Radius - Shadow.Offset.X);
			Gutter.Right = FMath::Max(Gutter.Right, Radius + Shadow.Offset.X);
			Gutter.Top = FMath::Max(Gutter.Top, Radius - Shadow.Offset.Y);
			Gutter.Bottom = FMath::Max(Gutter.Bottom, Radius + Shadow.Offset.Y);
		}
		const FBox2D ContentBounds = SubtreePaintBounds(*Node, false);
		Gutter.Left = FMath::CeilToFloat(Gutter.Left + FMath::Max(0.0, -ContentBounds.Min.X));
		Gutter.Top = FMath::CeilToFloat(Gutter.Top + FMath::Max(0.0, -ContentBounds.Min.Y));
		Gutter.Right = FMath::CeilToFloat(Gutter.Right + FMath::Max(0.0, ContentBounds.Max.X - Node->Transform.Width.Value));
		Gutter.Bottom = FMath::CeilToFloat(Gutter.Bottom + FMath::Max(0.0, ContentBounds.Max.Y - Node->Transform.Height.Value));
		FUMGWidgetNode Retainer;
		Retainer.Type = UMGWidgetTypes::RetainerBox;
		Retainer.Id = Widget.Id + TEXT("_subtreeshadow");
		Retainer.Name = Widget.Name + TEXT("_subtreeshadow");
		Retainer.Bounds.W = Node->Transform.Width.Value + Gutter.Left + Gutter.Right;
		Retainer.Bounds.H = Node->Transform.Height.Value + Gutter.Top + Gutter.Bottom;
		Retainer.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Retainer.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		Retainer.Slot.Padding = FMargin(-Gutter.Left, -Gutter.Top, -Gutter.Right, -Gutter.Bottom);
		Retainer.Effect.bRetainRendering = true;
		Retainer.Effect.EffectMaterial = UMGPropertyKeys::RetainerSubtreeShadowEffectRef;
		Retainer.Effect.TextureParameter = TEXT("Texture");
		Retainer.Shadow.BoxShadows = ShadowLayers;
        Retainer.Render.PaintBlendMode = GlyphPaintBlendMode;
        if (InnerShadow)
        {
            const float CaptureWidth = FMath::Max(1.0f, static_cast<float>(Retainer.Bounds.W));
            const float CaptureHeight = FMath::Max(1.0f, static_cast<float>(Retainer.Bounds.H));
            const float Blur = FMath::Max(0.0f, InnerShadow->Blur) * BlurStrengthToUVStepScale;
            Retainer.Effect.EffectColor = FUMGValueParser::ParseColor(
                InnerShadow->Color.IsEmpty() ? DesignColorUtils::DefaultShadowColorHex : InnerShadow->Color);
            Retainer.Effect.EffectOffset = FVector2D(InnerShadow->OffsetX / CaptureWidth, InnerShadow->OffsetY / CaptureHeight);
            Retainer.Effect.EffectBlurUVStep = FVector2D(Blur / CaptureWidth, Blur / CaptureHeight);
            if (!InnerShadow->BlendMode.IsEmpty() && VisualMapperFillTransfer::CanonicalPaintBlendMode(InnerShadow->BlendMode) != TEXT("normal"))
                Retainer.Effect.InnerShadowBlendMode = VisualMapperFillTransfer::CanonicalPaintBlendMode(InnerShadow->BlendMode);
        }

		FUMGWidgetNode Padded;
		Padded.Type = UMGWidgetTypes::Border;
		Padded.Id = Widget.Id + TEXT("_shadowpadding");
		Padded.Name = Widget.Name + TEXT("_shadowpadding");
		Padded.Brush.BrushColor = FLinearColor::Transparent;
		Padded.WidgetStyle.Padding = Gutter;
		Widget.Id += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Name += Design2UMGGeneratedNameSuffixes::Content();
		Widget.Bounds = FDesignPixelBounds();
		Widget.Slot = FUMGSlotData();
		Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
		Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;
		Padded.Children.Add(MoveTemp(Widget));
		Retainer.Children.Add(MoveTemp(Padded));
		Wrapper.Children.Add(MoveTemp(Retainer));
		return Wrapper;
	}

	// All shadow layers live in this single overlay. Later source layers render
	// below earlier ones, matching the stacking the retired per-layer nesting produced.
	for (int32 Index = ShadowLayers.Num() - 1; Index >= 0; --Index)
	{
		Wrapper.Children.Add(BuildBoxShadowLayerImage(Node, ShadowLayers[Index], Index, Widget.Id, Widget.Name));
	}

	// Place the original widget above the shadows.
	Widget.Id = Widget.Id + Design2UMGGeneratedNameSuffixes::Content();
	Widget.Name = Widget.Name + Design2UMGGeneratedNameSuffixes::Content();
	Widget.Bounds = FDesignPixelBounds();
	Widget.Slot = FUMGSlotData();
	Widget.Slot.HorizontalAlignment = EUMGSlotHorizontalAlignment::Fill;
	Widget.Slot.VerticalAlignment = EUMGSlotVerticalAlignment::Fill;

	Wrapper.Children.Add(MoveTemp(Widget));
	return Wrapper;
}

FVector4 UMGRetainerCapture::FillInsets(const FDesignNode& Node)
{
    const FVector2D Size(Node.Transform.Width.Value, Node.Transform.Height.Value);
    const FBox2D Paint = IncludeOuterShadows(Node, FBox2D(FVector2D::ZeroVector, Size));
    return FVector4(FMath::CeilToFloat(FMath::Max(0.0, -Paint.Min.X)),
                    FMath::CeilToFloat(FMath::Max(0.0, -Paint.Min.Y)),
                    FMath::CeilToFloat(FMath::Max(0.0, Paint.Max.X - Size.X)),
                    FMath::CeilToFloat(FMath::Max(0.0, Paint.Max.Y - Size.Y)));
}

FMargin VisualMapperFillTransfer::BackgroundCaptureInsets(const FDesignNode& Node)
{
    const FVector2D Size(Node.Transform.Width.Value, Node.Transform.Height.Value);
    const FBox2D Paint = SubtreePaintBounds(Node, true);
    return FMargin(FMath::CeilToFloat(FMath::Max(0.0, -Paint.Min.X)),
        FMath::CeilToFloat(FMath::Max(0.0, -Paint.Min.Y)),
        FMath::CeilToFloat(FMath::Max(0.0, Paint.Max.X - Size.X)),
        FMath::CeilToFloat(FMath::Max(0.0, Paint.Max.Y - Size.Y)));
}

FUMGWidgetNode UMGRetainerCapture::Wrap(FUMGWidgetNode&& Retainer,const FVector2D& LogicalSize,const FMargin& Gutter)
{
    if(Gutter==FMargin(0))return MoveTemp(Retainer);
    FUMGWidgetNode Content=MoveTemp(Retainer.Children[0]);Retainer.Children.Reset();
    FUMGWidgetNode Wrapper;
    Wrapper.Type=UMGWidgetTypes::Overlay;Wrapper.Id=Retainer.Id+TEXT("_capture");Wrapper.Name=Retainer.Name+TEXT("_capture");
    Wrapper.Bounds=Retainer.Bounds;Wrapper.Slot=Retainer.Slot;Wrapper.Render=MoveTemp(Retainer.Render);
    Retainer.Render=FUMGRenderStyleData();Retainer.Bounds=FDesignPixelBounds();Retainer.Slot=FUMGSlotData();
    Retainer.Bounds.W=LogicalSize.X+Gutter.Left+Gutter.Right;Retainer.Bounds.H=LogicalSize.Y+Gutter.Top+Gutter.Bottom;
    Retainer.Slot.HorizontalAlignment=EUMGSlotHorizontalAlignment::Fill;
    Retainer.Slot.VerticalAlignment=EUMGSlotVerticalAlignment::Fill;
    Retainer.Slot.Padding=FMargin(-Gutter.Left,-Gutter.Top,-Gutter.Right,-Gutter.Bottom);
    FUMGWidgetNode Padded;
    Padded.Type=UMGWidgetTypes::Border;Padded.Id=Retainer.Id+TEXT("_padding");Padded.Name=Retainer.Name+TEXT("_padding");
    Padded.Brush.BrushColor=FLinearColor::Transparent;Padded.WidgetStyle.Padding=Gutter;
    Padded.Children.Add(MoveTemp(Content));Retainer.Children.Add(MoveTemp(Padded));Wrapper.Children.Add(MoveTemp(Retainer));
    return Wrapper;
}
