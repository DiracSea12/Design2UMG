// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverterVisualSupport.h"

#include "Design2UMGLog.h"
#include "DesignColorUtils.h"
#include "LayoutMapper.h"
#include "VisualMapperFillTransfer.h"
#include "UMGWidgetTypes.h"
#include "UMGValueParser.h"

namespace
{
	// Placeholder tint thresholds: low alpha or a small RGB channel spread counts as neutral.
	constexpr uint8 NeutralPlaceholderAlphaThreshold = 0x20;
	constexpr uint8 NeutralPlaceholderChannelTolerance = 0x10;
	// Semantic placeholder surfaces are tinted only from this size upwards.
	constexpr float PlaceholderSurfaceMinWidth = 24.0f;
	constexpr float PlaceholderSurfaceMinHeight = 12.0f;
	// Fill alpha used when the placeholder falls back to its outline color.
	constexpr uint8 PlaceholderFallbackFillAlpha = 0x33;
	// Placeholder inset: min(upper bound, width * fraction, height * fraction),
	// clamped to [min, upper bound].
	constexpr int32 PlaceholderInsetMaxPx = 10;
	constexpr int32 PlaceholderInsetMinPx = 1;
	constexpr float PlaceholderInsetSizeFraction = 0.25f;

	// Accept rule is unchanged from before the hex-literal consolidation: unlike
	// DesignColorUtils::IsHexColorLiteral (which validates every char is a hex
	// digit), this shape check only tests the '#' prefix plus the
	// '#RGB'/'#RRGGBB'/'#RRGGBBAA' length whitelist, and deliberately skips
	// hex-digit validation.
	bool MatchesHexColorShape(const FString& ColorValue)
	{
		if (!ColorValue.StartsWith(TEXT("#")))
		{
			return false;
		}

		const int32 Len = ColorValue.Len();
		return Len == 4 || Len == 7 || Len == 9;
	}

	FString BuildHexColorWithAlpha(const FString& ColorValue, uint8 Alpha)
	{
		if (!MatchesHexColorShape(ColorValue))
		{
			return FString();
		}

		FColor Parsed = DesignColorUtils::ParseHexColor(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!MatchesHexColorShape(ColorValue))
		{
			return true;
		}

		const FColor Parsed = DesignColorUtils::ParseHexColor(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= NeutralPlaceholderAlphaThreshold)
			|| ((MaxChannel - MinChannel) <= NeutralPlaceholderChannelTolerance);
	}

}

namespace Design2UMGConverterVisualSupport
{
	bool HasVisualFill(const TSharedPtr<FDesignNode>& Node)
	{
		return Node.IsValid() && Node->Visual.Fills.Num() > 0;
	}

	bool ShouldUseRenderFallback(const TSharedPtr<FDesignNode>& Node)
	{
		if (!Node.IsValid()
			|| Node->Visual.RenderFallback.ImageRef.IsEmpty()
			|| Node->Visual.RenderFallback.Scope == EDesignRenderFallbackScope::Unsupported
			|| Node->Visual.RenderFallback.bSuppressedBySourceSemantics)
		{
			return false;
		}

		if (Node->Type == EDesignNodeType::Text
			|| Node->Type == EDesignNodeType::IconFont
			|| Node->Type == EDesignNodeType::Ref)
		{
			return false;
		}

		return Node->Children.Num() == 0
			|| Node->Visual.RenderFallback.Scope == EDesignRenderFallbackScope::Subtree;
	}

	FUMGWidgetNode CreateRenderFallbackWidget(const TSharedPtr<FDesignNode>& Node)
	{
		FUMGWidgetNode Widget;
		Widget.Type = UMGWidgetTypes::Image;
		Widget.Brush.Image.TexturePath = Node->Visual.RenderFallback.ImageRef;
		if (Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
		return Widget;
	}

	void ApplyBaseVisualProperties(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		VisualMapper::ApplyVisualProperties(Widget, Node);
		// Direct typed reads with descriptor-identical presence rules:
		// MaterialRef presence also covers a gradient-owned brush, and
		// TexturePath lives on the canonical image owner.
		const bool bHasBorderBackground =
			Widget.Brush.BrushColor.IsSet()
			|| Widget.Brush.Image.TexturePath.IsSet()
			|| Widget.Brush.Gradient.HasAny()
			|| Widget.Brush.MaterialRef.IsSet();
		if (Widget.Type == UMGWidgetTypes::Border && !bHasBorderBackground)
		{
			Widget.Brush.BrushColor = FLinearColor::Transparent;
			if (Widget.Brush.OutlineThickness.IsSet())
			{
				Widget.Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
			}
		}
	}

	void ApplyRenderFallbackVisuals(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node)
	{
		VisualMapper::ApplyRenderTransformProperties(Widget, Node);
	}

	bool TryApplyPlaceholderSurfaceTint(
		FUMGWidgetNode& Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FConversionVisualSettingsPayload& VisualSettings)
	{
		const UDesign2UMGSettings::FResolvedPlaceholderHatchStyle& Style = VisualSettings.PlaceholderHatchStyle;
		const bool bExpandedRefInstancePlaceholder = Node->bExpandedRefInstance;
		const FString& DefaultPlaceholderFillColor =
			bExpandedRefInstancePlaceholder ? Style.RefFillColor : Style.BaseFillColor;
		const FString& DefaultPlaceholderOutlineColor =
			bExpandedRefInstancePlaceholder ? Style.RefStripeColor : Style.BaseStripeColor;
		const bool bIsLeafContainer =
			(Node->Type == EDesignNodeType::Frame
				|| Node->Type == EDesignNodeType::Group
				|| Node->Type == EDesignNodeType::Rectangle)
			&& Node->Children.Num() == 0;
		// Tint only semantic empty surfaces; authored non-placeholder art should stay on the normal visual path.
		const bool bUseSlotPlaceholderTint =
			bIsLeafContainer
			&& Node->Transform.Width.Value >= PlaceholderSurfaceMinWidth
			&& Node->Transform.Height.Value >= PlaceholderSurfaceMinHeight
			&& Node->LayoutData.bSemanticPlaceholderSurface;
		if (!bUseSlotPlaceholderTint)
		{
			return false;
		}

		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		const FString FillColor = MatchesHexColorShape(PrimaryFill.SolidColor) ? PrimaryFill.SolidColor : FString();
		const FString StrokeColor = (Node->Visual.Stroke.Fill.IsSet() && MatchesHexColorShape(Node->Visual.Stroke.Fill.GetValue().SolidColor))
			? Node->Visual.Stroke.Fill.GetValue().SolidColor
			: FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
		{
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, PlaceholderFallbackFillAlpha);
		}
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
		{
			return false;
		}

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = UMGWidgetTypes::Image;
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(
				static_cast<float>(PlaceholderInsetMaxPx),
				Node->Transform.Width.Value * PlaceholderInsetSizeFraction,
				Node->Transform.Height.Value * PlaceholderInsetSizeFraction)),
			PlaceholderInsetMinPx,
			PlaceholderInsetMaxPx);
		Widget.Brush = FUMGVisualBrushData();
		Widget.Brush.MaterialRef = TEXT("placeholdersurface");
		FUMGVisualBrushData::FShapeData Shape;
		Shape.Color = FUMGValueParser::ParseColor(PlaceholderFillColor);
		Shape.StripeColor = FUMGValueParser::ParseColor(PlaceholderOutlineColor);
		Shape.StripeWidth = Style.StripeWidthPx;
		Shape.StripeGap = Style.StripeGapPx;
		Shape.Inset = InsetPx;
		Widget.Brush.Shape = MoveTemp(Shape);
		if (Node->Transform.Width.Value > 0.0f)
		{
			Widget.WidgetStyle.WidthOverride = Node->Transform.Width.Value;
		}
		if (Node->Transform.Height.Value > 0.0f)
		{
			Widget.WidgetStyle.HeightOverride = Node->Transform.Height.Value;
		}
		return true;
	}

	bool ShouldWrapNodeWithBorder(
		const TSharedPtr<FDesignNode>& Node,
		bool bPlaceholderUsesDirectImage)
	{
		// Parser-owned padding decisions win; the fallback only covers layouts where runtime still owns the inset.
		const bool bNeedsPaddingWrapper =
			LayoutMapper::HasPadding(Node)
			&& Node->LayoutData.PaddingRequiresRuntimeWrap.Get(
				Node->LayoutData.Layout == EDesignLayout::None
					|| ((Node->LayoutData.Layout == EDesignLayout::Wrap || Node->LayoutData.Layout == EDesignLayout::Grid)
						&& Node->Children.Num() > 0));
		// Multi-fill creates its own compositing surface. A separate Border is only
		// needed when it still owns padding, corner, or stroke semantics.
		const bool bMultiFillOwnsSurface =
			Node->HasMultiFill()
			&& !bNeedsPaddingWrapper
			&& !Node->Visual.CornerRadius.HasRadius()
			&& !Node->Visual.Stroke.HasStroke();

		return (Node->Type == EDesignNodeType::Frame || Node->Type == EDesignNodeType::Group)
			&& ((HasVisualFill(Node) && !bMultiFillOwnsSurface)
				|| bNeedsPaddingWrapper
				|| Node->Visual.CornerRadius.HasRadius()
				|| Node->Visual.Stroke.HasStroke())
			&& !bPlaceholderUsesDirectImage;
	}

	FUMGWidgetNode WrapWithBorder(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		bool& OutExplicitStrokePainted)
	{
		return VisualMapper::WrapWithBorder(MoveTemp(Widget), Node, &OutExplicitStrokePainted);
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& VisualMapperOptions,
		bool bExplicitStrokePainted)
	{
		int32 PaintLayerCount = 0;
		if (VisualMapperFillTransfer::RequiresBackdropLayers(Widget, Node) || Node->HasMultiFill() || Node->Visual.Fills.ContainsByPredicate(
            [](const FDesignFill& Fill) { return Fill.bEnabled && Fill.Type == EDesignFillType::Shader; }))
		{
			Widget = VisualMapperFillTransfer::WrapWithMultiFill(MoveTemp(Widget), Node, VisualMapperOptions, &PaintLayerCount);
		}

        const bool bBlendedStroke = Node->Visual.Stroke.Fill.IsSet()
            && Node->Visual.Stroke.Fill->Type != EDesignFillType::Shader
            && !Node->Visual.Stroke.Fill->BlendMode.IsEmpty()
            && VisualMapperFillTransfer::CanonicalPaintBlendMode(Node->Visual.Stroke.Fill->BlendMode) != TEXT("normal");
        if (!bExplicitStrokePainted || bBlendedStroke)
			Widget = VisualMapperFillTransfer::WrapWithPaintedStroke(MoveTemp(Widget), Node, PaintLayerCount);

		if (Node->Visual.GeometryMask.HasMask())
		{
			Widget = VisualMapper::WrapWithGeometryMask(MoveTemp(Widget), Node);
		}

		// UMG has one inner-shadow surface here, so choose the strongest blur as the visible representative.
		const FDesignShadow* InnerShadow = nullptr;
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Type != EDesignEffectType::Shadow || !Effect.Shadow.bInner)
			{
				continue;
			}

			if (!InnerShadow || Effect.Shadow.Blur > InnerShadow->Blur)
			{
				InnerShadow = &Effect.Shadow;
			}
		}

        const auto NonNormal = [](const FString& Mode)
        { return !Mode.IsEmpty() && VisualMapperFillTransfer::CanonicalPaintBlendMode(Mode) != TEXT("normal"); };
        const bool bCombinedGlyphShadows = InnerShadow
            && (Node->Type == EDesignNodeType::Text || Node->Type == EDesignNodeType::IconFont)
            && (NonNormal(Widget.Render.PaintBlendMode.Get(FString())) || NonNormal(InnerShadow->BlendMode)
                || Widget.Shadow.BoxShadows.ContainsByPredicate([&](const FUMGBoxShadowLayerData& Layer)
                    { return NonNormal(Layer.BlendMode); }));

		if (InnerShadow && !bCombinedGlyphShadows && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = VisualMapper::WrapWithInnerShadow(MoveTemp(Widget), Node, *InnerShadow);
		}

		if (Widget.Shadow.BoxShadows.Num() > 0 || bCombinedGlyphShadows)
		{
			const TArray<FUMGBoxShadowLayerData> BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			Widget = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, BoxShadows, bCombinedGlyphShadows ? InnerShadow : nullptr);
		}

		float MaxForegroundBlur = 0.0f;
		float MaxBackgroundBlur = 0.0f;
		// Blur wrappers are ordered after shadows and use the maximum radius to avoid stacking duplicate blur passes.
		for (const FDesignEffect& Effect : Node->Visual.Effects)
		{
			if (!Effect.bEnabled || Effect.Radius <= 0.0f)
			{
				continue;
			}

			if (Effect.Type == EDesignEffectType::Blur)
			{
				MaxForegroundBlur = FMath::Max(MaxForegroundBlur, Effect.Radius);
			}
			else if (Effect.Type == EDesignEffectType::BackgroundBlur)
			{
				MaxBackgroundBlur = FMath::Max(MaxBackgroundBlur, Effect.Radius);
			}
		}

		if (MaxForegroundBlur > 0.0f && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = VisualMapper::WrapWithForegroundBlur(MoveTemp(Widget), Node, MaxForegroundBlur);
		}

		if (MaxBackgroundBlur > 0.0f)
		{
			Widget = VisualMapper::WrapWithBackgroundBlur(MoveTemp(Widget), MaxBackgroundBlur);
		}

        for(const auto& Effect:Node->Visual.Effects)
        {
            if(!Effect.bEnabled || Effect.Type!=EDesignEffectType::Shader || !Effect.Shader.IsSet()) continue;
            Widget=VisualMapper::WrapWithShaderEffect(MoveTemp(Widget),Node,Effect.Shader.GetValue());
        }
		VisualMapperFillTransfer::MarkBackdropProducers(Widget, Node);
		return Widget;
	}
}
