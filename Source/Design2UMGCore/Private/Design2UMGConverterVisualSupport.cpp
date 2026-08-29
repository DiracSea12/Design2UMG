// Copyright (c) 2025-2026, Diracsea12. All Rights Reserved. SPDX-License-Identifier: MIT

#include "Design2UMGConverterVisualSupport.h"

#include "Design2UMGLog.h"
#include "LayoutMapper.h"

namespace
{
	bool IsHexColorLiteral(const FString& ColorValue)
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
		if (!IsHexColorLiteral(ColorValue))
		{
			return FString();
		}

		FColor Parsed = FColor::FromHex(ColorValue);
		Parsed.A = Alpha;
		return FString::Printf(TEXT("#%02X%02X%02X%02X"), Parsed.R, Parsed.G, Parsed.B, Parsed.A);
	}

	bool IsNeutralPlaceholderColor(const FString& ColorValue)
	{
		if (!IsHexColorLiteral(ColorValue))
		{
			return true;
		}

		const FColor Parsed = FColor::FromHex(ColorValue);
		const uint8 MaxChannel = FMath::Max3(Parsed.R, Parsed.G, Parsed.B);
		const uint8 MinChannel = FMath::Min3(Parsed.R, Parsed.G, Parsed.B);
		return (Parsed.A <= 0x20) || ((MaxChannel - MinChannel) <= 0x10);
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
		Widget.Type = TEXT("Image");
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
		if (Widget.Type == TEXT("Border") && !bHasBorderBackground)
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
			&& Node->Transform.Width.Value >= 24.0f
			&& Node->Transform.Height.Value >= 12.0f
			&& Node->LayoutData.bSemanticPlaceholderSurface;
		if (!bUseSlotPlaceholderTint)
		{
			return false;
		}

		const FDesignFill& PrimaryFill = Node->Visual.TopmostFill();
		const FString FillColor = IsHexColorLiteral(PrimaryFill.SolidColor) ? PrimaryFill.SolidColor : FString();
		const FString StrokeColor = (Node->Visual.Stroke.Fill.IsSet() && IsHexColorLiteral(Node->Visual.Stroke.Fill.GetValue().SolidColor))
			? Node->Visual.Stroke.Fill.GetValue().SolidColor
			: FString();
		const FString PlaceholderOutlineColor =
			(!StrokeColor.IsEmpty() && !IsNeutralPlaceholderColor(StrokeColor)) ? StrokeColor : DefaultPlaceholderOutlineColor;
		FString PlaceholderFillColor =
			(!FillColor.IsEmpty() && !IsNeutralPlaceholderColor(FillColor)) ? FillColor : DefaultPlaceholderFillColor;
		if (PlaceholderFillColor.IsEmpty() && !PlaceholderOutlineColor.IsEmpty())
		{
			PlaceholderFillColor = BuildHexColorWithAlpha(PlaceholderOutlineColor, 0x33);
		}
		if (PlaceholderFillColor.IsEmpty() || PlaceholderOutlineColor.IsEmpty())
		{
			return false;
		}

		UE_LOG(LogDesign2UMG, Verbose, TEXT("ConvertNode: applying placeholder surface tint to '%s'"), *Node->Name);
		Widget.Type = TEXT("Image");
		Widget.Children.Empty();
		const int32 InsetPx = FMath::Clamp(
			FMath::RoundToInt(FMath::Min3(10.0f, Node->Transform.Width.Value * 0.25f, Node->Transform.Height.Value * 0.25f)),
			1,
			10);
		Widget.Brush = FUMGVisualBrushData();
		Widget.Brush.MaterialRef = TEXT("placeholdersurface");
		Widget.Brush.MaterialParams = FString::Printf(
			TEXT("{\"Color\":\"%s\",\"StripeColor\":\"%s\",\"StripeWidth\":%d,\"StripeGap\":%d,\"Inset\":%d}"),
			*PlaceholderFillColor,
			*PlaceholderOutlineColor,
			Style.StripeWidthPx,
			Style.StripeGapPx,
			InsetPx);
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
		const TSharedPtr<FDesignNode>& Node)
	{
		return VisualMapper::WrapWithBorder(MoveTemp(Widget), Node);
	}

	FUMGWidgetNode ApplyVisualEffectWrappers(
		FUMGWidgetNode Widget,
		const TSharedPtr<FDesignNode>& Node,
		const FVisualMapperOptions& VisualMapperOptions)
	{
		if (Node->HasMultiFill())
		{
			Widget = VisualMapper::WrapWithMultiFill(MoveTemp(Widget), Node, VisualMapperOptions);
		}

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

		if (InnerShadow && Node.IsValid() && Node->Transform.Width.Value > 0.0f && Node->Transform.Height.Value > 0.0f)
		{
			Widget = VisualMapper::WrapWithInnerShadow(MoveTemp(Widget), Node, *InnerShadow);
		}

		if (Widget.Shadow.BoxShadows.Num() > 0)
		{
			const TArray<FUMGBoxShadowLayerData> BoxShadows = Widget.Shadow.BoxShadows;
			Widget.Shadow.BoxShadows.Reset();
			Widget = VisualMapper::WrapWithBoxShadow(MoveTemp(Widget), Node, BoxShadows);
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

		return Widget;
	}
}
